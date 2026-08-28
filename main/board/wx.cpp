#include "wx.hpp"

#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs.h"

#include "state.hpp"

static const char *TAG = "wx";

namespace {

constexpr const char *kNamespace = "riddle";
constexpr const char *kKey       = "weather";

// open-meteo. The vendor's weather API cannot serve a non-Chinese location --
// its city codes are 447 Chinese cities and nothing else -- which is why the
// Waveshare build moved here too.
constexpr const char *kUrl =
    "https://api.open-meteo.com/v1/forecast"
    "?latitude=" WX_LAT "&longitude=" WX_LON
    "&current=temperature_2m,weather_code"
    "&daily=temperature_2m_max,temperature_2m_min"
    "&timezone=auto&forecast_days=1";

// 4KB. The response is about 600 bytes; this is headroom, not a guess.
constexpr size_t kMaxSize = 4096;

struct ctx_t { char *buf; size_t cap; size_t len; bool over; };

esp_err_t on_event(esp_http_client_event_t *e)
{
    if (e->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    auto *c = (ctx_t *)e->user_data;
    if (!c) return ESP_OK;
    const size_t room = (c->cap - 1) - c->len;
    const size_t take = ((size_t)e->data_len < room) ? (size_t)e->data_len : room;
    if (take < (size_t)e->data_len) c->over = true;
    std::memcpy(c->buf + c->len, e->data, take);
    c->len += take;
    c->buf[c->len] = '\0';
    return ESP_OK;
}

}  // namespace

bool wx_load(weather_t *out)
{
    if (!out || !state_nvs_init()) return false;
    std::memset(out, 0, sizeof *out);

    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = sizeof *out;
    weather_t tmp;
    const esp_err_t err = nvs_get_blob(h, kKey, &tmp, &len);
    nvs_close(h);

    if (err != ESP_OK || len != sizeof tmp) return false;
    *out = tmp;
    ESP_LOGI(TAG, "cache: %d.%dC, wmo %u", tmp.temp_x10 / 10,
             (tmp.temp_x10 < 0 ? -tmp.temp_x10 : tmp.temp_x10) % 10,
             (unsigned)tmp.wmo);
    return true;
}

bool wx_fetch(weather_t *out, uint32_t now_utc)
{
    if (!out) return false;

    static char buf[kMaxSize];      // static: 4KB is too much for the stack
    buf[0] = '\0';
    ctx_t ctx = { buf, sizeof buf, 0, false };

    esp_http_client_config_t cfg = {};
    cfg.url               = kUrl;
    cfg.event_handler     = on_event;
    cfg.user_data         = &ctx;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms        = 15000;
    cfg.buffer_size       = 2048;
    cfg.buffer_size_tx    = 1024;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    const esp_err_t err = esp_http_client_perform(client);
    const int status    = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) { ESP_LOGW(TAG, "fetch: %s", esp_err_to_name(err)); return false; }
    if (status != 200) { ESP_LOGW(TAG, "HTTP %d", status); return false; }
    if (ctx.over)      { ESP_LOGW(TAG, "response too large; ignoring"); return false; }

    // Parse into a local. weather_parse leaves its output untouched on
    // failure, so a malformed response cannot clobber a good cached value --
    // and a wrong temperature on a wall is worse than an old one.
    weather_t w;
    if (!weather_parse(buf, &w)) {
        ESP_LOGW(TAG, "response did not parse; keeping the cache");
        return false;
    }
    w.fetched_at = now_utc;
    *out = w;

    nvs_handle_t h;
    if (state_nvs_init() && nvs_open(kNamespace, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, kKey, &w, sizeof w);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "%d.%dC, %s (wmo %u), hi/lo %d/%d",
             w.temp_x10 / 10, (w.temp_x10 < 0 ? -w.temp_x10 : w.temp_x10) % 10,
             wmo_label(w.wmo), (unsigned)w.wmo, w.hi_x10 / 10, w.lo_x10 / 10);
    return true;
}
