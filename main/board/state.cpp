#include "state.hpp"

#include <cstring>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "state";

namespace {

constexpr const char *kNamespace = "riddle";
constexpr const char *kKey       = "state";

bool nvs_ready()
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

}  // namespace

bool state_load(riddle_nvs_t *st)
{
    if (!st) return false;
    std::memset(st, 0, sizeof *st);
    st->guess = RIDDLE_NO_GUESS;        // zero would mean "chose A"

    if (!nvs_ready()) return false;

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
    if (!st || !nvs_ready()) return false;

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
