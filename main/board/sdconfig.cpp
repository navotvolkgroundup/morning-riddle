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
// A NEW KEY, not the old one. The blob went from one timetable to four, so an
// old "sched" value is the wrong size and nvs_get_exact refuses it -- which is
// correct but silent. A new name makes the migration a fact rather than a
// mystery: the old key is simply never read again.
constexpr const char *kKeySched  = "scheds";

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

void sdconfig_store_kids(const kids_t *kids)
{
    if (kids && state_nvs_init()) nvs_put_blob(kKeyKids, kids, sizeof *kids);
}

void sdconfig_store_schedule(const kids_schedule_t *sched)
{
    if (sched && state_nvs_init()) nvs_put_blob(kKeySched, sched, sizeof *sched);
}

void sdconfig_load_cached(kids_t *kids, kids_schedule_t *sched)
{
    if (kids)  std::memset(kids,  0, sizeof *kids);
    if (sched) std::memset(sched, 0, sizeof *sched);
    if (!state_nvs_init()) return;
    if (kids)  nvs_get_exact(kKeyKids,  kids,  sizeof *kids);
    if (sched) nvs_get_exact(kKeySched, sched, sizeof *sched);
}

void sdconfig_load(kids_t *kids, kids_schedule_t *sched)
{
    if (!kids || !sched) return;
    std::memset(kids, 0, sizeof *kids);
    std::memset(sched, 0, sizeof *sched);

    if (!state_nvs_init()) return;

    // Cache first, so a board with no card still knows the names.
    if (nvs_get_exact(kKeyKids, kids, sizeof *kids)) {
        ESP_LOGI(TAG, "kids from cache: %d", kids->count);
        // Names and byte lengths, so a name that LOOKS cut on the panel can be
        // told apart from one that was cut on the way in. Local serial only.
        for (int i = 0; i < kids->count; i++)
            ESP_LOGI(TAG, "  kid %d: \"%s\" (%d bytes) birthday %u/%u", i,
                     kids->kid[i].name, (int)strlen(kids->kid[i].name),
                     (unsigned)kids->kid[i].birth_day,
                     (unsigned)kids->kid[i].birth_month);
    }
    if (nvs_get_exact(kKeySched, sched, sizeof *sched)) {
        for (int k = 0; k < KIDS_MAX; k++)
            for (int d = 0; d < SCHED_DAYS; d++)
                if (sched->kid[k].line[d][0])
                    ESP_LOGI(TAG, "  kid %d day %d: \"%s\" (%d bytes)", k, d,
                             sched->kid[k].line[d],
                             (int)strlen(sched->kid[k].line[d]));
    }

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
    //
    // NOT IMPORTED ANY MORE. The card held one household timetable and there
    // are four now, one per child, which would need a new file format. This
    // board's reader has never completed a data-block transfer -- SPI and
    // SDMMC both fail at sdmmc_init_sd_ssr, before any filesystem -- so
    // designing a format for a path that has never once worked is effort spent
    // on a hypothetical. Timetables come from the portal.

    // If either file was expected and missing, say what IS on the card. "No
    // such file" plus a listing answers the next question immediately.
    if (kids->count == 0) sd_list_root();
}
