#include "sdcard.hpp"

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd";

namespace {
constexpr int kPinClk  = 15;
constexpr int kPinMosi = 13;
constexpr int kPinMiso = 14;
constexpr int kPinCs   = 47;

sdmmc_card_t *g_card = nullptr;
bool g_bus_ready = false;
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

    const esp_err_t err =
        esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot, &mcfg, &g_card);
    if (err != ESP_OK) {
        // No card is the ordinary case, so it is logged quietly; anything else
        // is worth noticing.
        if (err == ESP_ERR_NOT_FOUND || err == ESP_FAIL)
            ESP_LOGI(TAG, "no usable card (%s)", esp_err_to_name(err));
        else
            ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
        g_card = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "mounted %s, %lluMB", SD_MOUNT_POINT,
             ((uint64_t)g_card->csd.capacity) * g_card->csd.sector_size / (1024 * 1024));
    return true;
}

void sd_unmount()
{
    if (!g_card) return;
    esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, g_card);
    g_card = nullptr;
}
