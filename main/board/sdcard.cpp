#include "sdcard.hpp"

#include <M5Unified.h>

#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include <dirent.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sd";

namespace {
constexpr int kPinClk  = 15;
constexpr int kPinMosi = 13;
constexpr int kPinMiso = 14;
constexpr int kPinCs   = 47;

sdmmc_card_t *g_card = nullptr;
bool g_bus_ready = false;

// The card's power rail is switched by the PM1, not by the ESP32: register
// 0x11 bit 3, set up as an output by M5Unified's Power init for this board.
//
// That means a card that has wedged -- pulled while powered, or left in a bad
// state after a half-finished transaction -- can be POWER CYCLED in software.
// Nothing else in the SPI layer can recover such a card: it simply stops
// answering, and every retry gets the same silence.
constexpr uint8_t kPm1Addr    = 0x6E;
constexpr uint8_t kPm1GpioOut = 0x11;
constexpr uint8_t kTfPowerBit = 1 << 3;

void tf_power_cycle()
{
    ESP_LOGI(TAG, "power-cycling the card via the PM1");
    M5.In_I2C.bitOff(kPm1Addr, kPm1GpioOut, kTfPowerBit, 100000);
    vTaskDelay(pdMS_TO_TICKS(250));      // let the rail actually fall
    M5.In_I2C.bitOn(kPm1Addr, kPm1GpioOut, kTfPowerBit, 100000);
    vTaskDelay(pdMS_TO_TICKS(250));      // and the card finish its own reset
}
}  // namespace

bool sd_mount()
{
    if (g_card) return true;

    if (!g_bus_ready) {
        spi_bus_config_t bus = {};
        bus.mosi_io_num     = kPinMosi;
        bus.miso_io_num     = kPinMiso;
        bus.sclk_io_num     = kPinClk;
        bus.quadwp_io_num   = -1;
        bus.quadhd_io_num   = -1;
        bus.max_transfer_sz = 4000;

        // SPI3, not SPI2: M5GFX has already claimed SPI2 for the panel, and
        // asking for it again returns ESP_ERR_INVALID_STATE. The S3 routes any
        // pin through the GPIO matrix, so the card's pins do not care which
        // controller drives them.
        const esp_err_t err = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
            return false;
        }
        g_bus_ready = true;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;
    // Default speed. 400kHz was tried on the theory that slower is safer and
    // it made things worse, not better -- sdmmc_init_sd_ssr timed out where
    // the default had mounted this same card at 14910MB earlier. Speculative
    // robustness that is not measured is just a change.
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    // A GENEROUS COMMAND TIMEOUT. The failure here is not "no card": it is
    // sdmmc_init_sd_ssr timing out, and that is a LATE step -- the card has
    // already answered CMD0, CMD8 and ACMD41 and handed over its CID and CSD
    // to get that far. An absent card fails long before this.
    //
    // So one command (ACMD13, reading the SD Status Register) is slow to
    // answer, and the default timeout gives up on it. Cards vary enormously
    // here; the cost of waiting longer is milliseconds on a path that runs
    // twice a day.
    host.command_timeout_ms = 3000;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = (gpio_num_t)kPinCs;
    slot.host_id = SPI3_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mcfg = {};
    // format_if_mount_failed stays FALSE. This card carries the only copy of
    // the kids' names and the timetable; a firmware bug that reformats it
    // because a read went wrong is not a recovery, it is data loss.
    mcfg.format_if_mount_failed = false;
    mcfg.max_files              = 4;
    mcfg.allocation_unit_size   = 16 * 1024;

    // Three attempts. A card that has just been powered up, or reinserted
    // while the board is running, often refuses the first probe and answers
    // the second -- retrying is cheaper than telling someone their card is
    // broken when it is not.
    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= 3; attempt++) {
        err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot, &mcfg, &g_card);
        if (err == ESP_OK) break;
        ESP_LOGW(TAG, "mount attempt %d: %s", attempt, esp_err_to_name(err));

        // Cut the card's power before trying again. A retry at the same power
        // state just repeats the same conversation with a card that is not
        // listening; taking the rail down is what makes the next attempt
        // genuinely different.
        tf_power_cycle();
    }

    // SDMMC FALLBACK.
    //
    // SPI failed at sdmmc_init_sd_ssr, which is the FIRST data-block transfer
    // in the init sequence -- commands and responses work, a 512-byte read
    // does not. That points at the data path rather than the card.
    //
    // The S3 can drive these same pins through its SDMMC peripheral, which is
    // a completely different data path: CLK, CMD and D0 instead of SCLK, MOSI
    // and MISO, with D3 held high in place of chip select. If SPI cannot read
    // a block and SDMMC can, that is worth knowing and worth using.
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPI mode failed; trying SDMMC 1-line on the same pins");
        if (g_bus_ready) { spi_bus_free(SPI3_HOST); g_bus_ready = false; }

        sdmmc_host_t mhost = SDMMC_HOST_DEFAULT();
        mhost.flags        = SDMMC_HOST_FLAG_1BIT;
        mhost.max_freq_khz = SDMMC_FREQ_PROBING;
        mhost.command_timeout_ms = 3000;

        sdmmc_slot_config_t mslot = SDMMC_SLOT_CONFIG_DEFAULT();
        mslot.width = 1;
        mslot.clk   = (gpio_num_t)kPinClk;    // G15
        mslot.cmd   = (gpio_num_t)kPinMosi;   // G13 doubles as CMD
        mslot.d0    = (gpio_num_t)kPinMiso;   // G14 doubles as D0
        mslot.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

        err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &mhost, &mslot, &mcfg, &g_card);
        if (err == ESP_OK)
            ESP_LOGW(TAG, "SDMMC worked where SPI did not");
    }

    if (err != ESP_OK) {
        // ESP_ERR_TIMEOUT means the card never answered. That is what an empty
        // slot looks like AND what a badly seated or failing card looks like,
        // so say both rather than asserting one.
        if (err == ESP_ERR_TIMEOUT)
            ESP_LOGW(TAG, "the card stopped answering partway through init "
                          "(%s) -- present but not completing, not absent",
                     esp_err_to_name(err));
        else if (err == ESP_FAIL)
            ESP_LOGW(TAG, "card answered but has no readable filesystem -- "
                          "format it as FAT32 (MS-DOS), not exFAT");
        else
            ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
        g_card = nullptr;
        return false;
    }

    // Report the FILESYSTEM's size, not just the card's. The two disagree in
    // the case that cost three boot cycles: an exFAT card reports its full
    // block capacity here and then presents an empty root, so "mounted,
    // 14910MB" looked like success while nothing could be read. A filesystem
    // that reports 0 total bytes is not a filesystem this build understands.
    uint64_t total = 0, freeb = 0;
    const esp_err_t ferr = esp_vfs_fat_info(SD_MOUNT_POINT, &total, &freeb);
    ESP_LOGI(TAG, "card %lluMB; filesystem %lluMB total, %lluMB free",
             ((uint64_t)g_card->csd.capacity) * g_card->csd.sector_size / (1024 * 1024),
             total / (1024 * 1024), freeb / (1024 * 1024));
    if (ferr != ESP_OK || total == 0) {
        ESP_LOGE(TAG, "the card mounted but has no readable filesystem -- "
                      "format it as FAT32 (MS-DOS), not exFAT");
    }
    return true;
}

void sd_list_root()
{
    if (!g_card) { ESP_LOGW(TAG, "not mounted"); return; }
    DIR *d = opendir(SD_MOUNT_POINT);
    if (!d) { ESP_LOGW(TAG, "cannot open %s", SD_MOUNT_POINT); return; }
    ESP_LOGI(TAG, "contents of %s:", SD_MOUNT_POINT);
    int n = 0;
    for (struct dirent *e; (e = readdir(d)) != nullptr; ) {
        ESP_LOGI(TAG, "  %s%s", e->d_name, (e->d_type == DT_DIR) ? "/" : "");
        if (++n >= 32) { ESP_LOGI(TAG, "  ... (more)"); break; }
    }
    if (n == 0) ESP_LOGI(TAG, "  (empty)");
    closedir(d);
}

void sd_unmount()
{
    if (!g_card) return;
    esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, g_card);
    g_card = nullptr;
}
