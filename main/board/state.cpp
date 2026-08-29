#include "state.hpp"

#include <cstring>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "state";

namespace {

constexpr const char *kNamespace = "riddle";
constexpr const char *kKey       = "state";

}  // namespace

bool state_nvs_init()
{
    static bool tried = false;
    static bool ok    = false;
    if (tried) return ok;
    tried = true;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // A partition that cannot be mounted is erased and remade. That loses
        // the streak, which is a shame but not a fault; refusing to run at all
        // because a counter is unreadable would be worse.
        ESP_LOGW(TAG, "NVS unusable (%s); erasing and recreating",
                 esp_err_to_name(err));
        if (nvs_flash_erase() == ESP_OK) err = nvs_flash_init();
    }
    ok = (err == ESP_OK);
    if (!ok) ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
    return ok;
}

void state_note_sleeping()
{
    if (!state_nvs_init()) return;
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "slept", 1);
    nvs_commit(h);
    nvs_close(h);
}

void state_note_awake(int vbus_mv, bool button_held)
{
    if (!state_nvs_init()) return;
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i16(h, "awvbus", (int16_t)vbus_mv);
    nvs_set_u8(h, "awbtn", button_held ? 1 : 0);
    nvs_set_u8(h, "awake", 1);
    nvs_commit(h);
    nvs_close(h);
}

void state_note_wake(int cause, int button)
{
    if (!state_nvs_init()) return;
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;

    uint8_t prev_cause = 255, prev_btn = 255, slept = 0;
    nvs_get_u8(h, "lastcause", &prev_cause);
    nvs_get_u8(h, "lastbtn", &prev_btn);
    nvs_get_u8(h, "slept", &slept);
    uint8_t awake = 0, awbtn = 0; int16_t awvbus = -1;
    nvs_get_u8(h, "awake", &awake);
    nvs_get_u8(h, "awbtn", &awbtn);
    nvs_get_i16(h, "awvbus", &awvbus);
    ESP_LOGW(TAG, "PREVIOUS boot: cause=%d button=%d, reached deep sleep: %s",
             (int)(int8_t)prev_cause, (int)(int8_t)prev_btn, slept ? "YES" : "NO");
    if (!slept)
        ESP_LOGW(TAG, "  it refused to sleep: reached the decision=%s, "
                      "VBUS=%dmV, button held=%s",
                 awake ? "YES" : "NO (never got there)",
                 (int)awvbus, awbtn ? "YES" : "no");
    nvs_set_u8(h, "slept", 0);
    nvs_set_u8(h, "awake", 0);

    nvs_set_u8(h, "lastcause", (uint8_t)cause);
    nvs_set_u8(h, "lastbtn", (uint8_t)(int8_t)button);
    nvs_commit(h);
    nvs_close(h);
}

uint32_t state_bump_boot_count()
{
    if (!state_nvs_init()) return 0;
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return 0;

    uint32_t n = 0;
    nvs_get_u32(h, "boots", &n);        // absent on a fresh device: stays 0
    n++;
    nvs_set_u32(h, "boots", n);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGW(TAG, "boot #%u", (unsigned)n);
    return n;
}

bool state_load(riddle_nvs_t *st)
{
    if (!st) return false;
    std::memset(st, 0, sizeof *st);
    st->guess = RIDDLE_NO_GUESS;        // zero would mean "chose A"

    if (!state_nvs_init()) return false;

    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "no stored state yet; starting idle");
        return true;                    // first boot: not an error
    }

    riddle_nvs_t tmp;
    size_t len = sizeof tmp;
    const esp_err_t err = nvs_get_blob(h, kKey, &tmp, &len);
    nvs_close(h);

    // Size must match exactly. A short or long blob means the struct changed
    // shape between builds, and reading it as the current layout would silently
    // scramble the day and streak rather than fail.
    if (err == ESP_OK && len == sizeof tmp) {
        *st = tmp;
        ESP_LOGI(TAG, "loaded: day=%ld idx=%u state=%u guess=%d streak=%u",
                 (long)st->day, (unsigned)st->idx, (unsigned)st->state,
                 (int)st->guess, (unsigned)st->streak);
    } else if (err == ESP_OK) {
        ESP_LOGW(TAG, "stored state is %u bytes, expected %u -- ignoring it",
                 (unsigned)len, (unsigned)sizeof tmp);
    }
    return true;
}

bool state_save(const riddle_nvs_t *st)
{
    if (!st || !state_nvs_init()) return false;

    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "cannot open NVS for writing");
        return false;
    }

    esp_err_t err = nvs_set_blob(h, kKey, st, sizeof *st);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "saved: day=%ld state=%u guess=%d streak=%u",
             (long)st->day, (unsigned)st->state, (int)st->guess,
             (unsigned)st->streak);
    return true;
}
