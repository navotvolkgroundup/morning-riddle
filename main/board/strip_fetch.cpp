#include "strip_fetch.hpp"

#include <cstdio>
#include <cstring>

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"

#include "batch.hpp"        // BATCH_URL, for the release it lives beside

namespace {
const char *TAG = "strip";

struct sink {
    uint8_t *buf;
    size_t   cap;
    size_t   len;
    bool     overflow;
};

esp_err_t on_event(esp_http_client_event_t *e)
{
    if (e->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    auto *s = (sink *)e->user_data;
    if (!s) return ESP_OK;
    // A body larger than the cap is a malformed strip, and the right response
    // is to stop copying and let strip_parse reject it -- not to grow a buffer
    // for whatever the network handed us.
    if (s->len + (size_t)e->data_len > s->cap) { s->overflow = true; return ESP_OK; }
    std::memcpy(s->buf + s->len, e->data, (size_t)e->data_len);
    s->len += (size_t)e->data_len;
    return ESP_OK;
}
}  // namespace

bool strip_fetch(int idx, uint8_t *buf, size_t cap, strip_t *out)
{
    if (!buf || !out || idx < 0) return false;

    // Same release, sibling asset. Deriving the URL from BATCH_URL rather than
    // repeating the host keeps the two from drifting apart the day the release
    // tag changes.
    static const char kBase[] = BATCH_URL;
    const char *slash = std::strrchr(kBase, '/');
    if (!slash) return false;
    char url[sizeof kBase + 32];
    const int n = std::snprintf(url, sizeof url, "%.*s/strip-%d.bin",
                                (int)(slash - kBase), kBase, idx);
    if (n <= 0 || n >= (int)sizeof url) return false;

    sink s = { buf, cap, 0, false };

    esp_http_client_config_t cfg = {};
    cfg.url               = url;
    cfg.event_handler     = on_event;
    cfg.user_data         = &s;
    cfg.timeout_ms        = 15000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    // The release URL 302s to a signed objects.githubusercontent.com URL. The
    // client follows that by default, which is what batch.cpp already relies on.

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;
    const esp_err_t err = esp_http_client_perform(c);
    const int status    = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    if (err != ESP_OK) {
        ESP_LOGI(TAG, "no picture today: %s", esp_err_to_name(err));
        return false;
    }
    if (status == 404) {
        // The normal case. Most items have no picture.
        ESP_LOGI(TAG, "no picture for riddle %d", idx);
        return false;
    }
    if (status != 200) {
        ESP_LOGI(TAG, "no picture today: HTTP %d", status);
        return false;
    }
    if (s.overflow) {
        ESP_LOGW(TAG, "picture larger than %u bytes; skipping", (unsigned)cap);
        return false;
    }
    if (!strip_parse(buf, s.len, out)) {
        // Truncation lands here, which is the whole reason strip_parse checks
        // the length: half an image drawn across the top of the page would sit
        // there for a day.
        ESP_LOGW(TAG, "picture did not parse (%u bytes); skipping",
                 (unsigned)s.len);
        return false;
    }
    ESP_LOGI(TAG, "picture %ux%u (%u bytes)", out->w, out->h, (unsigned)s.len);
    return true;
}
