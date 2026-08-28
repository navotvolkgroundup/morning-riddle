#include "sdconfig.hpp"

#include <cstring>

#include "esp_log.h"
#include "nvs.h"

#include "sdcard.hpp"
#include "state.hpp"

extern "C" {
#include "sd_json.h"
}

static const char *TAG = "sdconfig";

namespace {

constexpr const char *kNamespace = "riddle";
constexpr const char *kKeyKids   = "kids";
constexpr const char *kKeySched  = "sched";

constexpr const char *kPathKids  = SD_MOUNT_POINT "/kids.json";
constexpr const char *kPathSched = SD_MOUNT_POINT "/schedule.json";

// Reads a blob whose size must match exactly. A blob of another size means the
// struct changed shape between builds, and reinterpreting it would hand the
// renderer names and subjects made of noise.
bool nvs_get_exact(const char *key, void *out, size_t want)
{
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = want;
    const esp_err_t err = nvs_get_blob(h, key, out, &len);
    nvs_close(h);
    return err == ESP_OK && len == want;
}

void nvs_put_blob(const char *key, const void *data, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "cannot open NVS to cache %s", key);
        return;
    }
    nvs_set_blob(h, key, data, len);
    nvs_commit(h);
    nvs_close(h);
}

}  // namespace

void sdconfig_load(kids_t *kids, schedule_t *sched)
{
    if (!kids || !sched) return;
    std::memset(kids, 0, sizeof *kids);
    std::memset(sched, 0, sizeof *sched);

    if (!state_nvs_init()) return;

    // Cache first, so a board with no card still knows the names.
    if (nvs_get_exact(kKeyKids, kids, sizeof *kids))
        ESP_LOGI(TAG, "kids from cache: %d", kids->count);
    if (nvs_get_exact(kKeySched, sched, sizeof *sched))
        ESP_LOGI(TAG, "schedule from cache: %s",
                 schedule_is_empty(sched) ? "empty" : "present");

    if (!sd_mount()) return;            // no card: the cache stands

    // ---- kids.json ----
    {
        static char buf[2048];          // not the stack: kids_t is large
        const sdj_status_e rd = sdj_read(kPathKids, buf, sizeof buf, nullptr);
        if (rd == SDJ_OK) {
            static kids_t parsed;
            if (kids_parse(buf, &parsed) && kids_valid(&parsed)) {
                if (std::memcmp(&parsed, kids, sizeof parsed) != 0) {
                    *kids = parsed;
                    nvs_put_blob(kKeyKids, &parsed, sizeof parsed);
                    ESP_LOGI(TAG, "imported %d kid(s) from the card", parsed.count);
                }
            } else {
                ESP_LOGW(TAG, "%s did not parse; keeping the cached names",
                         kPathKids);
            }
        } else if (rd != SDJ_ABSENT) {
            ESP_LOGW(TAG, "%s: %s", kPathKids, sdj_strerror(rd));
        }
    }

    // ---- schedule.json ----
    {
        static char buf[2048];
        const sdj_status_e rd = sdj_read(kPathSched, buf, sizeof buf, nullptr);
        if (rd == SDJ_OK) {
            static schedule_t parsed;
            if (schedule_parse(buf, &parsed)) {
                if (std::memcmp(&parsed, sched, sizeof parsed) != 0) {
                    *sched = parsed;
                    nvs_put_blob(kKeySched, &parsed, sizeof parsed);
                    ESP_LOGI(TAG, "imported a timetable from the card");
                }
            } else {
                ESP_LOGW(TAG, "%s did not parse; keeping the cached timetable",
                         kPathSched);
            }
        } else if (rd != SDJ_ABSENT) {
            ESP_LOGW(TAG, "%s: %s", kPathSched, sdj_strerror(rd));
        }
    }

    // If either file was expected and missing, say what IS on the card. "No
    // such file" plus a listing answers the next question immediately.
    if (kids->count == 0 && schedule_is_empty(sched)) sd_list_root();
}
