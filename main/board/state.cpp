#include "state.hpp"

#include <cstring>

#include "esp_log.h"
#include "nvs.h"
#include "esp_app_desc.h"
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

uint32_t state_config_fingerprint(const kids_t *k, const kids_schedule_t *s)
{
    // FNV-1a over both blobs. Not a checksum against corruption -- NVS already
    // does that -- just "is this the same config I drew last time".
    uint32_t h = 2166136261u;
    const uint8_t *b;
    if (k) { b = (const uint8_t *)k; for (size_t i = 0; i < sizeof *k; i++) { h ^= b[i]; h *= 16777619u; } }
    if (s) { b = (const uint8_t *)s; for (size_t i = 0; i < sizeof *s; i++) { h ^= b[i]; h *= 16777619u; } }

    // AND THE FIRMWARE, because the firmware is part of what is on the panel.
    //
    // The fingerprint asks "is the page on the wall still the page I would
    // draw?" and the config was only half the answer: every layout change
    // today -- the newspaper masthead, the weather panel, the drop cap -- left
    // a board that would have kept showing yesterday's rendering until the
    // riddle changed, because the names and the timetable had not moved.
    //
    // Mixing the build hash in means a reflash always redraws exactly once,
    // which costs one 17s refresh on a cabled session and is the behaviour
    // every one of those flashes wanted. It is also what would have shown the
    // black-page bug immediately instead of on the next quiet morning.
    const esp_app_desc_t *app = esp_app_get_description();
    if (app) {
        b = (const uint8_t *)app->app_elf_sha256;
        for (size_t i = 0; i < sizeof app->app_elf_sha256; i++) { h ^= b[i]; h *= 16777619u; }
    }
    return h;
}

uint32_t state_drawn_config()
{
    if (!state_nvs_init()) return 0;
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return 0;
    uint32_t fp = 0;
    nvs_get_u32(h, "cfgdrawn", &fp);
    nvs_close(h);
    return fp;
}

void state_set_drawn_config(uint32_t fp)
{
    if (!state_nvs_init()) return;
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, "cfgdrawn", fp);
    nvs_commit(h);
    nvs_close(h);
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
