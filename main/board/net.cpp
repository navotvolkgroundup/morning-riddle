#include "net.hpp"

#include <M5Unified.h>

#include <cstring>
#include <ctime>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "sdcard.hpp"
#include "portal.hpp"

extern "C" {
#include "sd_json.h"
}

static const char *TAG = "net";

namespace {

constexpr const char *kCredPath = SD_MOUNT_POINT "/wifi.json";
constexpr int kGotIp = BIT0;
constexpr int kFailed = BIT1;

EventGroupHandle_t g_events = nullptr;
bool g_started = false;
int  g_retries = 0;

void on_wifi(void *, esp_event_base_t base, int32_t id, void *)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        // A handful of retries, then give up and let the caller carry on from
        // cache. A board that blocks forever on a missing network is a board
        // that never shows the page.
        if (++g_retries <= 3) {
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(g_events, kFailed);
        }
    }
}

void on_ip(void *, esp_event_base_t, int32_t, void *data)
{
    auto *e = (ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "got " IPSTR, IP2STR(&e->ip_info.ip));
    xEventGroupSetBits(g_events, kGotIp);
}

// Reads ssid/pass from the card. Returns false and logs the reason when the
// file is missing or malformed -- deliberately WITHOUT logging the password.
bool read_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    // NVS FIRST. Credentials typed into the setup portal live here, and a
    // device on a wall should not need its card pulled to change networks.
    // The card stays as a fallback -- useful for provisioning a board before
    // it is mounted, and for the data that genuinely belongs on a card.
    if (portal_load_credentials(ssid, ssid_len, pass, pass_len)) {
        ESP_LOGI(TAG, "credentials from NVS for \"%s\"", ssid);
        return true;
    }

    if (!sd_mount()) {
        ESP_LOGW(TAG, "no SD card, so no credentials");
        return false;
    }

    char buf[512];
    const sdj_status_e rd = sdj_read(kCredPath, buf, sizeof buf, nullptr);
    if (rd != SDJ_OK) {
        ESP_LOGW(TAG, "%s: %s", kCredPath, sdj_strerror(rd));
        sd_list_root();          // say what IS there, not just what is not
        return false;
    }

    cJSON *root = cJSON_Parse(buf);
    // Wipe the buffer as soon as it is parsed. The password was on the stack
    // and this costs nothing.
    std::memset(buf, 0, sizeof buf);
    if (!root) {
        ESP_LOGW(TAG, "%s did not parse", kCredPath);
        return false;
    }

    const cJSON *s = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(root, "pass");
    bool ok = cJSON_IsString(s) && s->valuestring[0] && cJSON_IsString(p);
    if (ok) {
        std::strncpy(ssid, s->valuestring, ssid_len - 1);
        std::strncpy(pass, p->valuestring, pass_len - 1);
        ssid[ssid_len - 1] = '\0';
        pass[pass_len - 1] = '\0';
        // SSID only. The password is never logged, at any level.
        ESP_LOGI(TAG, "credentials for \"%s\"", ssid);
    } else {
        ESP_LOGW(TAG, "%s needs string \"ssid\" and \"pass\"", kCredPath);
    }
    cJSON_Delete(root);
    return ok;
}

}  // namespace

bool net_connect(int timeout_ms)
{
    char ssid[33] = {}, pass[65] = {};
    if (!read_credentials(ssid, sizeof ssid, pass, sizeof pass)) return false;

    if (!g_events) g_events = xEventGroupCreate();
    xEventGroupClearBits(g_events, kGotIp | kFailed);
    g_retries = 0;

    if (!g_started) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();

        wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&init));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi, nullptr, nullptr));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip, nullptr, nullptr));
        g_started = true;
    }

    wifi_config_t cfg = {};
    std::strncpy((char *)cfg.sta.ssid, ssid, sizeof cfg.sta.ssid - 1);
    std::strncpy((char *)cfg.sta.password, pass, sizeof cfg.sta.password - 1);
    // Scrub the locals now that the driver has its own copy.
    std::memset(ssid, 0, sizeof ssid);
    std::memset(pass, 0, sizeof pass);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    std::memset(&cfg, 0, sizeof cfg);
    ESP_ERROR_CHECK(esp_wifi_start());

    const EventBits_t bits = xEventGroupWaitBits(
        g_events, kGotIp | kFailed, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (bits & kGotIp) return true;
    ESP_LOGW(TAG, "no network (%s); carrying on from cache",
             (bits & kFailed) ? "connect failed" : "timed out");
    return false;
}

bool net_sync_time(int timeout_ms)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    if (esp_netif_sntp_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "sntp init failed");
        return false;
    }

    const bool ok = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms)) == ESP_OK;
    esp_netif_sntp_deinit();

    if (!ok) {
        ESP_LOGW(TAG, "NTP timed out; the clock stays as it was");
        return false;
    }

    const time_t now = time(nullptr);
    struct tm utc;
    gmtime_r(&now, &utc);

    // WRITE THE RTC, not just the system clock. The system clock dies with the
    // power and this board deep-sleeps between wakes, so a sync that only set
    // the system clock would be forgotten before it was ever used.
    if (M5.Rtc.isEnabled()) {
        M5.Rtc.setDateTime(&utc);
        ESP_LOGI(TAG, "RTC set to %04d-%02d-%02d %02d:%02d:%02d UTC",
                 utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                 utc.tm_hour, utc.tm_min, utc.tm_sec);
    } else {
        ESP_LOGW(TAG, "no RTC to write; the time will not survive sleep");
    }
    return true;
}

void net_stop()
{
    if (!g_started) return;
    esp_wifi_disconnect();
    esp_wifi_stop();
}
