#include "batch.hpp"

#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs.h"

#include "state.hpp"

static const char *TAG = "batch";

namespace {

constexpr const char *kNamespace = "riddle";
constexpr const char *kKey       = "batch";

// 32KB. A 30-riddle batch is about 7KB, so this is roughly 4x headroom -- and
// unlike a buffer sized to the partition, an absurd response fails fast
// instead of being parsed.
constexpr size_t kMaxSize = 32 * 1024;

struct fetch_ctx {
    char  *buf;
    size_t cap;
    size_t len;
    bool   overflowed;
};

esp_err_t on_event(esp_http_client_event_t *e)
{
    if (e->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    auto *c = (fetch_ctx *)e->user_data;
    if (!c) return ESP_OK;

    const size_t room = (c->cap - 1) - c->len;
    const size_t take = ((size_t)e->data_len < room) ? (size_t)e->data_len : room;
    if (take < (size_t)e->data_len) c->overflowed = true;
    std::memcpy(c->buf + c->len, e->data, take);
    c->len += take;
    c->buf[c->len] = '\0';
    return ESP_OK;
}

}  // namespace

int batch_load(riddle_batch_t *out)
{
    if (!out || !state_nvs_init()) return 0;
    std::memset(out, 0, sizeof *out);

    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return 0;

    size_t len = sizeof *out;
    // STATIC, NOT ON THE STACK. riddle_batch_t is ~19KB (40 riddles of ~470
    // bytes) and the main task stack is 10KB. A local overflows it and
    // corrupts the heap, which surfaces as "realloc() pointer is outside heap
    // areas" from cJSON -- an assert that points nowhere near the cause.
    static riddle_batch_t tmp;
    const esp_err_t err = nvs_get_blob(h, kKey, &tmp, &len);
    nvs_close(h);

    // Exact size only. A blob of another size means the struct changed shape
    // between builds; reading it as the current layout would hand the renderer
    // garbage that looks like text.
    if (err != ESP_OK || len != sizeof tmp) return 0;

    *out = tmp;
    ESP_LOGI(TAG, "cache holds %d riddle(s)", out->count);
    return out->count;
}

int batch_fetch(riddle_batch_t *out)
{
    if (!out) return -1;

    char *buf = (char *)heap_caps_malloc(kMaxSize, MALLOC_CAP_SPIRAM);
    if (!buf) { ESP_LOGE(TAG, "no PSRAM for the batch buffer"); return -1; }
    buf[0] = '\0';

    fetch_ctx ctx = { buf, kMaxSize, 0, false };

    esp_http_client_config_t cfg = {};
    cfg.url               = BATCH_URL;
    cfg.event_handler     = on_event;
    cfg.user_data         = &ctx;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms        = 20000;
    // 4096, not the 512 default. A GitHub release download answers with a 302
    // whose Location is a signed objects.githubusercontent.com URL; the
    // X-Amz signature alone runs past 512, so the header parse overflows
    // before the redirect is followed. It fails as "Out of buffer" and then a
    // transport error, which reads as a network problem and is not one. The
    // Waveshare build lost real time to this.
    cfg.buffer_size       = 4096;
    cfg.buffer_size_tx    = 2048;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { heap_caps_free(buf); return -1; }

    const esp_err_t err = esp_http_client_perform(client);
    const int status    = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    int n = -1;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "fetch failed: %s", esp_err_to_name(err));
    } else if (status != 200) {
        ESP_LOGE(TAG, "HTTP %d", status);
    } else if (ctx.overflowed) {
        // Truncation is reported, never parsed. A partial JSON document either
        // fails to parse or -- worse -- parses into a shorter batch that looks
        // entirely legitimate.
        ESP_LOGE(TAG, "batch exceeded %u bytes; refusing it", (unsigned)kMaxSize);
    } else {
        ESP_LOGI(TAG, "fetched %u bytes", (unsigned)ctx.len);
        static riddle_batch_t parsed;   // ~19KB: far too big for the stack
        n = riddle_batch_parse(buf, &parsed);
        if (n <= 0) {
            ESP_LOGE(TAG, "batch did not parse; keeping the cached one");
            n = -1;
        } else {
            if (parsed.skipped)
                ESP_LOGW(TAG, "skipped %d malformed item(s)", parsed.skipped);
            ESP_LOGI(TAG, "batch holds %d riddle(s)", n);
            *out = parsed;

            nvs_handle_t h;
            if (state_nvs_init() && nvs_open(kNamespace, NVS_READWRITE, &h) == ESP_OK) {
                nvs_set_blob(h, kKey, &parsed, sizeof parsed);
                nvs_commit(h);
                nvs_close(h);
            } else {
                ESP_LOGW(TAG, "fetched but could not cache; will refetch");
            }
        }
    }

    heap_caps_free(buf);
    return n;
}
