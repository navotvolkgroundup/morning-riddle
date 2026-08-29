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

// NO POWER CYCLING. This file used to toggle PM1 register 0x11 bit 3 between
// mount attempts, on the belief that it was the card's power rail and that
// cutting it could revive a wedged card.
//
// THE BIT WAS A GUESS, AND REGISTER 0x11 IS THE WRONG THING TO TOUCH.
//
// M5Stack's PaperColor documentation lists the PM1's GPIO functions as:
//
//   PYG0  E-Paper power enable          <-- THE PANEL
//   PYG1  microSD card insertion detect
//   PYG2  RTC interrupt
//   PYG3  microSD power supply
//   PYG4  microSD detection enable
//
// So a card power rail does exist. But 0x11 is the GPIO OUTPUT register for
// all of them, and bitOn/bitOff is a read-modify-write: read the byte, change
// one bit, write it back. Any read that returns garbage writes zeros across
// every line in it -- including PYG0, the panel's own power. That is the most
// plausible account of a display that died and stayed dead across reboots
// while the PM1 held state.
//
// M5Unified muddies it further by driving this same register's bit 3 to enable
// the ES8311 codec (M5Unified.cpp:506 and :513), so the bit's meaning is not
// even consistent between the vendor's docs and the vendor's own code.
//
// None of which was checked before writing it. The original comment here
// asserted this bit was the card's rail, "set up as an output by M5Unified's
// Power init for this board", on no evidence at all.
//
// The cost was the worst bug of this port. Once the card began failing, the
// "recovery" ran four times on every boot -- three SPI attempts plus the SDMMC
// fallback -- and left the panel unable to render. The display initialised,
// cleared, reported successful pushes, and showed nothing, on every build
// including ones that had rendered perfectly days earlier. That last part is
// what made it so hard to see: the same binary changed behaviour, so the code
// looked innocent, and hours went into strapping pins, DMA heaps, autodetect
// caches and a suspected dead panel. M5Stack's factory firmware rendering
// fine is what finally turned it back into a software bug.
//
// The retries stay -- a card that is merely slow to settle does answer a
// second probe -- but nothing here touches a rail again. Do not add power
// control to this file without a datasheet reference for the exact bit.

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
            ESP_LOGW(TAG, "no answer from the card slot (%s) -- an EMPTY SLOT "
                          "and a failing card look identical here",
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
