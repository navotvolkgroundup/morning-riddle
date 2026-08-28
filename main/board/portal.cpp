#include "portal.hpp"

#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "portal";

namespace {

constexpr const char *kNamespace = "riddle";
constexpr const char *kKeySsid   = "wifi_ssid";
constexpr const char *kKeyPass   = "wifi_pass";
constexpr int kSubmitted = BIT0;

EventGroupHandle_t g_done = nullptr;
httpd_handle_t     g_http = nullptr;

// Deliberately plain. This page is typed into by a parent standing in a
// kitchen, on a phone, once. No external assets -- there is no internet on
// this network by definition, so anything remote would simply fail to load.
constexpr const char *kPage =
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Morning Riddle setup</title>"
    "<style>body{font-family:system-ui,sans-serif;margin:2rem auto;max-width:22rem;padding:0 1rem}"
    "h1{font-size:1.3rem}label{display:block;margin:1rem 0 .25rem}"
    "input{width:100%;padding:.6rem;font-size:1rem;box-sizing:border-box}"
    "button{margin-top:1.5rem;width:100%;padding:.8rem;font-size:1rem}"
    "p{color:#555;font-size:.9rem}</style>"
    "<h1>Morning Riddle</h1>"
    "<p>Which network should the board join?</p>"
    "<form method=POST action=/save>"
    "<label for=s>Network name</label><input id=s name=ssid autocapitalize=off autocorrect=off>"
    "<label for=p>Password</label><input id=p name=pass type=password>"
    "<button type=submit>Save</button></form>"
    "<p>Stored on the board only.</p>";

constexpr const char *kSaved =
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Saved</title><body style='font-family:system-ui,sans-serif;margin:2rem'>"
    "<h1>Saved</h1><p>The board is joining your network. You can close this page.</p>";

// Percent-decoding, in place. Form bodies arrive urlencoded and a password
// with a space, '&' or '+' in it is completely ordinary -- decoding it wrongly
// would store a subtly incorrect password and present as "the network refuses
// us", which is a miserable thing to debug from a wall.
void url_decode(char *s)
{
    char *o = s;
    for (char *i = s; *i; i++) {
        if (*i == '+') {
            *o++ = ' ';
        } else if (*i == '%' && i[1] && i[2]) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(i[1]), lo = hex(i[2]);
            if (hi >= 0 && lo >= 0) { *o++ = (char)(hi * 16 + lo); i += 2; }
            else                    { *o++ = *i; }
        } else {
            *o++ = *i;
        }
    }
    *o = '\0';
}

// Pulls one field out of an urlencoded body. Returns false if absent.
bool field(const char *body, const char *name, char *out, size_t out_len)
{
    char key[24];
    std::snprintf(key, sizeof key, "%s=", name);
    const char *p = std::strstr(body, key);
    if (!p) return false;
    p += std::strlen(key);
    const char *end = std::strchr(p, '&');
    size_t n = end ? (size_t)(end - p) : std::strlen(p);
    if (n >= out_len) n = out_len - 1;
    std::memcpy(out, p, n);
    out[n] = '\0';
    url_decode(out);
    return true;
}

esp_err_t get_root(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html");
    return httpd_resp_send(r, kPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t post_save(httpd_req_t *r)
{
    char body[512];
    int total = r->content_len < (int)sizeof body - 1 ? r->content_len
                                                      : (int)sizeof body - 1;
    int got = 0;
    while (got < total) {
        const int n = httpd_req_recv(r, body + got, total - got);
        if (n <= 0) { httpd_resp_send_500(r); return ESP_FAIL; }
        got += n;
    }
    body[got] = '\0';

    char ssid[33] = {}, pass[65] = {};
    const bool ok = field(body, "ssid", ssid, sizeof ssid) && ssid[0] &&
                    field(body, "pass", pass, sizeof pass);
    std::memset(body, 0, sizeof body);          // the password was in here

    if (!ok) {
        httpd_resp_set_type(r, "text/html");
        httpd_resp_send(r, kPage, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, kKeySsid, ssid);
        nvs_set_str(h, kKeyPass, pass);
        nvs_commit(h);
        nvs_close(h);
        // SSID only, ever.
        ESP_LOGI(TAG, "stored credentials for \"%s\"", ssid);
    } else {
        ESP_LOGE(TAG, "could not open NVS to store credentials");
    }
    std::memset(ssid, 0, sizeof ssid);
    std::memset(pass, 0, sizeof pass);

    httpd_resp_set_type(r, "text/html");
    httpd_resp_send(r, kSaved, HTTPD_RESP_USE_STRLEN);
    xEventGroupSetBits(g_done, kSubmitted);
    return ESP_OK;
}

// Anything else redirects to the form. Phones probe a known URL to detect
// captive portals; answering every path with the page is what makes the setup
// sheet pop up by itself instead of needing an IP typed in.
esp_err_t get_any(httpd_req_t *r)
{
    httpd_resp_set_status(r, "302 Found");
    httpd_resp_set_hdr(r, "Location", "http://192.168.4.1/");
    return httpd_resp_send(r, "", 0);
}

// Brings up netif, the event loop and the WiFi driver if nothing has yet.
//
// portal_run() cannot assume net_connect() did this. net_connect() returns
// early when there are no credentials -- which is exactly and only when the
// portal is wanted -- so on that path the driver has never been initialised.
// The first version assumed otherwise and abort()ed inside
// esp_netif_create_default_wifi_ap(), boot-looping the board.
//
// Every call here tolerates "already done": this runs whether or not a
// connection attempt came first.
void ensure_wifi_stack()
{
    static bool done = false;
    if (done) return;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        ESP_LOGW(TAG, "esp_netif_init: %s", esp_err_to_name(err));

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        ESP_LOGW(TAG, "event loop: %s", esp_err_to_name(err));

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE)
        ESP_LOGW(TAG, "esp_wifi_init: %s", esp_err_to_name(err));

    done = true;
}

}  // namespace

bool portal_load_credentials(char *ssid, size_t ssid_len,
                             char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;
    size_t sl = ssid_len, pl = pass_len;
    const bool ok = nvs_get_str(h, kKeySsid, ssid, &sl) == ESP_OK &&
                    nvs_get_str(h, kKeyPass, pass, &pl) == ESP_OK && ssid[0];
    nvs_close(h);
    return ok;
}

bool portal_have_credentials()
{
    char s[33], p[65];
    return portal_load_credentials(s, sizeof s, p, sizeof p);
}

bool portal_run(int timeout_ms)
{
    ESP_LOGW(TAG, "starting setup AP \"%s\" -- join it and open 192.168.4.1",
             PORTAL_AP_SSID);

    if (!g_done) g_done = xEventGroupCreate();
    xEventGroupClearBits(g_done, kSubmitted);

    ensure_wifi_stack();

    // Only once: creating the default AP netif twice aborts too.
    static esp_netif_t *ap_netif = nullptr;
    if (!ap_netif) ap_netif = esp_netif_create_default_wifi_ap();

    wifi_config_t ap = {};
    std::strncpy((char *)ap.ap.ssid, PORTAL_AP_SSID, sizeof ap.ap.ssid - 1);
    ap.ap.ssid_len       = std::strlen(PORTAL_AP_SSID);
    ap.ap.max_connection = 2;
    // Open, deliberately. A WPA password here would be a second secret to
    // communicate before the first one can be entered, and the only thing
    // reachable on this network is a form that stores what you type.
    ap.ap.authmode       = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
    hcfg.uri_match_fn = httpd_uri_match_wildcard;
    if (httpd_start(&g_http, &hcfg) != ESP_OK) {
        ESP_LOGE(TAG, "http server would not start");
        return false;
    }
    const httpd_uri_t root = { "/",     HTTP_GET,  get_root,  nullptr };
    const httpd_uri_t save = { "/save", HTTP_POST, post_save, nullptr };
    const httpd_uri_t any  = { "/*",    HTTP_GET,  get_any,   nullptr };
    httpd_register_uri_handler(g_http, &root);
    httpd_register_uri_handler(g_http, &save);
    httpd_register_uri_handler(g_http, &any);

    const EventBits_t bits = xEventGroupWaitBits(
        g_done, kSubmitted, pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    // Let the "Saved" page actually reach the phone before tearing the AP down.
    vTaskDelay(pdMS_TO_TICKS(1500));
    httpd_stop(g_http);
    g_http = nullptr;
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);   // leave the radio ready for a join

    const bool ok = (bits & kSubmitted) != 0;
    ESP_LOGW(TAG, "portal %s", ok ? "stored credentials" : "timed out");
    return ok;
}
