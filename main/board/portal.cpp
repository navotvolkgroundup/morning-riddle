#include "portal.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "sdconfig.hpp"

extern "C" {
#include "formdata.h"
}

static const char *TAG = "portal";

namespace {

constexpr const char *kNamespace = "riddle";
constexpr const char *kKeySsid   = "wifi_ssid";
constexpr const char *kKeyPass   = "wifi_pass";
constexpr int kSubmitted = BIT0;

EventGroupHandle_t g_done  = nullptr;
httpd_handle_t     g_http  = nullptr;
kids_t            *g_kids  = nullptr;
schedule_t        *g_sched = nullptr;

// Sunday first: the Israeli school week starts there, and schedule_t is
// indexed 0=Sunday to match schedule_weekday(). A form that listed Monday
// first would put every subject on the wrong day with no visible symptom.
const char *const kDayName[SCHED_DAYS] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

// Deliberately plain. This page is typed into by a parent standing in a
// kitchen, on a phone, once. No external assets -- there is no internet on
// this network by definition, so anything remote would simply fail to load.
constexpr const char *kHead =
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<meta charset=utf-8>"
    "<title>Morning Riddle setup</title>"
    "<style>body{font-family:system-ui,sans-serif;margin:1.5rem auto;max-width:24rem;padding:0 1rem}"
    "h1{font-size:1.3rem}h2{font-size:1rem;margin:2rem 0 0;border-top:1px solid #ddd;padding-top:1rem}"
    "label{display:block;margin:.9rem 0 .25rem;font-size:.9rem}"
    "input{width:100%;padding:.6rem;font-size:1rem;box-sizing:border-box}"
    ".b{display:flex;gap:.5rem}.b input{width:4rem}"
    "button{margin:1.5rem 0 3rem;width:100%;padding:.8rem;font-size:1rem}"
    "p{color:#555;font-size:.85rem}</style>"
    "<h1>Morning Riddle</h1><form method=POST action=/save>";

constexpr const char *kWifi =
    "<h2>WiFi</h2>"
    "<p>Leave blank to keep the current network.</p>"
    "<label for=s>Network name</label>"
    "<input id=s name=ssid autocapitalize=off autocorrect=off>"
    "<label for=p>Password</label><input id=p name=pass type=password>";

constexpr const char *kTail =
    "<button type=submit>Save</button></form>"
    "<p>Stored on the board only.</p>";

// Escapes into an HTML attribute. Hebrew passes through untouched -- it is
// UTF-8 and none of its bytes are special here -- but an apostrophe in a name
// or a subject would end the attribute early and silently truncate the field,
// which is exactly the kind of thing nobody tests with.
void esc(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 7 < out_len; p++) {
        const char *rep = nullptr;
        switch (*p) {
            case '&':  rep = "&amp;";  break;
            case '<':  rep = "&lt;";   break;
            case '>':  rep = "&gt;";   break;
            case '"':  rep = "&quot;"; break;
            case '\'': rep = "&#39;";  break;
            default:   out[o++] = (char)*p; continue;
        }
        const size_t n = std::strlen(rep);
        std::memcpy(out + o, rep, n);
        o += n;
    }
    out[o] = '\0';
}

// Builds the page into `buf`. Everything is prefilled from what the board
// currently holds, so submitting an untouched form stores exactly what was
// there -- a parent fixing one subject must not have to retype the week.
void render(char *buf, size_t cap)
{
    char v[SCHED_LINE_MAX * 6 + 8];
    size_t n = 0;
    auto add = [&](const char *fmt, ...) {
        if (n >= cap) return;
        va_list ap;
        va_start(ap, fmt);
        const int w = std::vsnprintf(buf + n, cap - n, fmt, ap);
        va_end(ap);
        if (w > 0) n += ((size_t)w < cap - n) ? (size_t)w : (cap - n);
    };

    add("%s%s", kHead, kWifi);

    if (g_kids) {
        add("<h2>Children</h2><p>Name, then birthday. Blank rows are ignored.</p>");
        for (int i = 0; i < KIDS_MAX; i++) {
            const bool have = i < g_kids->count;
            esc(have ? g_kids->kid[i].name : "", v, sizeof v);
            add("<label>Child %d</label><input name=k%dn maxlength=23 value=\"%s\">"
                "<div class=b>"
                "<input name=k%dm inputmode=numeric placeholder=month value=\"",
                i + 1, i, v, i);
            if (have && g_kids->kid[i].birth_month) add("%u", (unsigned)g_kids->kid[i].birth_month);
            add("\"><input name=k%dd inputmode=numeric placeholder=day value=\"", i);
            if (have && g_kids->kid[i].birth_day) add("%u", (unsigned)g_kids->kid[i].birth_day);
            add("\"></div>");
        }
    }

    if (g_sched) {
        add("<h2>Timetable</h2><p>Subjects for each day, separated by commas. "
            "Leave a day blank and nothing draws for it.</p>");
        for (int d = 0; d < SCHED_DAYS; d++) {
            esc(g_sched->line[d], v, sizeof v);
            add("<label>%s</label><input name=d%d value=\"%s\">", kDayName[d], d, v);
        }
    }

    add("%s", kTail);
}

constexpr const char *kSaved =
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<meta charset=utf-8><title>Saved</title>"
    "<body style='font-family:system-ui,sans-serif;margin:2rem'>"
    "<h1>Saved</h1><p>You can close this page. The board redraws in a moment.</p>";

// 12KB. Worst case is seven timetable lines of SCHED_LINE_MAX bytes plus four
// names, all HTML-escaped; static because a handler task's stack is 4KB.
char g_page[12288];

esp_err_t get_root(httpd_req_t *r)
{
    render(g_page, sizeof g_page);
    httpd_resp_set_type(r, "text/html");
    return httpd_resp_send(r, g_page, HTTPD_RESP_USE_STRLEN);
}

// 8KB. Hebrew urlencodes to nine bytes a letter (%D7%9E), so a full week of
// 128-byte subject lines plus four names is comfortably over the 512 the WiFi
// -only form used. Static, for the same stack reason as g_page.
char g_body[8192];

esp_err_t post_save(httpd_req_t *r)
{
    int total = r->content_len < (int)sizeof g_body - 1 ? r->content_len
                                                       : (int)sizeof g_body - 1;
    int got = 0;
    while (got < total) {
        const int n = httpd_req_recv(r, g_body + got, total - got);
        if (n <= 0) { httpd_resp_send_500(r); return ESP_FAIL; }
        got += n;
    }
    g_body[got] = '\0';

    // ---- WiFi. Blank SSID means "leave what is stored alone" -------------
    char ssid[33] = {}, pass[65] = {};
    if (form_field(g_body, "ssid", ssid, sizeof ssid) && ssid[0]) {
        form_field(g_body, "pass", pass, sizeof pass);
        nvs_handle_t h;
        if (nvs_open(kNamespace, NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_str(h, kKeySsid, ssid);
            nvs_set_str(h, kKeyPass, pass);
            nvs_commit(h);
            nvs_close(h);
            ESP_LOGI(TAG, "stored credentials for \"%s\"", ssid);   // SSID only, ever
        } else {
            ESP_LOGE(TAG, "could not open NVS to store credentials");
        }
        std::memset(pass, 0, sizeof pass);
    }

    // ---- Children --------------------------------------------------------
    //
    // Built into a fresh struct and swapped in whole. Editing g_kids in place
    // would leave a half-applied list behind if a later field were malformed,
    // and the page would then draw a child who is no longer in the form.
    if (g_kids) {
        kids_t k = {};
        for (int i = 0; i < KIDS_MAX; i++) {
            char name[KID_NAME_MAX] = {}, m[4] = {}, d[4] = {}, key[8];
            std::snprintf(key, sizeof key, "k%dn", i);
            if (!form_field(g_body, key, name, sizeof name) || !name[0]) continue;

            kid_t &kid = k.kid[k.count];
            std::strncpy(kid.name, name, sizeof kid.name - 1);
            std::snprintf(key, sizeof key, "k%dm", i);
            if (form_field(g_body, key, m, sizeof m)) kid.birth_month = (uint8_t)std::atoi(m);
            std::snprintf(key, sizeof key, "k%dd", i);
            if (form_field(g_body, key, d, sizeof d)) kid.birth_day = (uint8_t)std::atoi(d);

            // A birthday needs both halves to mean anything, and an
            // out-of-range one would make kids_valid() reject the whole list.
            if (kid.birth_month < 1 || kid.birth_month > 12 ||
                kid.birth_day   < 1 || kid.birth_day   > 31) {
                kid.birth_month = kid.birth_day = 0;
            }
            k.count++;
        }
        if (kids_valid(&k)) {
            *g_kids = k;
            sdconfig_store_kids(&k);
            ESP_LOGI(TAG, "stored %d child(ren)", k.count);
        } else {
            ESP_LOGW(TAG, "children rejected by kids_valid; keeping what was stored");
        }
    }

    // ---- Timetable -------------------------------------------------------
    //
    // Stored verbatim: schedule_t holds each day pre-joined, and comma-space
    // is exactly what the form asks for. No parse, no serialise.
    if (g_sched) {
        schedule_t sc = {};
        for (int d = 0; d < SCHED_DAYS; d++) {
            char key[4];
            std::snprintf(key, sizeof key, "d%d", d);
            form_field(g_body, key, sc.line[d], sizeof sc.line[d]);
        }
        *g_sched = sc;
        sdconfig_store_schedule(&sc);
        ESP_LOGI(TAG, "stored a timetable (%s)",
                 schedule_is_empty(&sc) ? "empty" : "present");
    }

    std::memset(g_body, 0, sizeof g_body);      // the password was in here

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

bool portal_run(kids_t *kids, schedule_t *sched, int timeout_ms)
{
    ESP_LOGW(TAG, "starting setup AP \"%s\" -- join it and open 192.168.4.1",
             PORTAL_AP_SSID);

    g_kids  = kids;
    g_sched = sched;

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
    // The default 4KB is tight now that a handler renders the whole form.
    hcfg.stack_size   = 8192;
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

    g_kids = nullptr; g_sched = nullptr;

    // Credentials, not "was submitted": a visit that only fixed a subject line
    // leaves the network exactly as it was, and the caller must not read that
    // as a board that still has nowhere to connect.
    const bool have = portal_have_credentials();
    ESP_LOGW(TAG, "portal %s; credentials %s",
             (bits & kSubmitted) ? "submitted" : "timed out",
             have ? "stored" : "still missing");
    return have;
}
