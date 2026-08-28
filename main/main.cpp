// Morning Riddle on the M5Paper Color -- first light.
//
// Goal of this file, for now: prove the panel draws, prove the colour space is
// what we think it is, and prove the ported C core links into a C++ main. It
// deliberately does NOT draw the daily page: daily_layout still carries the
// Waveshare board's portrait 480x800 constants and this panel is 600x400
// landscape, so a page drawn now would be wrong in a way that looks like a
// driver bug. Geometry comes next, on its own.

#include <M5Unified.h>

#include <cstdio>

#include "esp_log.h"
#include "esp_timer.h"

extern "C" {
#include "daily_layout.h"
#include "schedule.h"
}

#include "hebrew.hpp"
#include "page_daily.hpp"
#include "wake.hpp"

static const char *TAG = "riddle";

extern "C" void app_main(void)
{
    // Logged BEFORE anything else, so a silent board can be told apart from a
    // board that never reached app_main. Everything about this port's bring-up
    // has been guesswork for want of exactly this line.
    ESP_LOGW(TAG, "app_main entered");

    // WAKE CAUSE BEFORE M5.begin(), always. M5.begin() costs 52.7 seconds of
    // panel init, and a button press must be answered in milliseconds -- so a
    // button wake takes the short path and never touches the display.
    const wake_cause why = wake_why();
    ESP_LOGW(TAG, "wake cause = %d, button = %d", (int)why, wake_button_index());
    if (why == wake_cause::button) {
        // Feedback (LED on G21, chirp on G46) and recording the guess belong
        // here. Neither exists yet, so for now this only proves the short path
        // is taken -- and proves it by NOT spending 52 seconds.
        ESP_LOGW(TAG, "button wake: skipping the display entirely");
        wake_sleep();
    }

    auto cfg = M5.config();
    // Pin the board instead of trusting auto-detection. If detection fails the
    // display is never initialised and nothing draws -- silently, which is the
    // symptom we have.
    cfg.fallback_board = m5::board_t::board_M5PaperColor;
    // The page fills the screen itself, so M5's own clear is a wasted
    // full-panel waveform. Measuring whether it is a big part of the 52.7s.
    cfg.clear_display = false;
    const int64_t t_begin = esp_timer_get_time();
    M5.begin(cfg);
    ESP_LOGW(TAG, "M5.begin took %lld ms (clear_display=false)",
             (long long)((esp_timer_get_time() - t_begin) / 1000));
    ESP_LOGW(TAG, "M5.begin returned, board=%d", (int)M5.getBoard());

    const int w = M5.Display.width();
    const int h = M5.Display.height();
    // ESP_LOG, not printf. With CONFIG_ESP_CONSOLE_UART_CUSTOM the primary
    // console is the physical UART on IO5/IO4; only the logging macros are
    // mirrored to the secondary USB Serial/JTAG console. printf here goes out
    // pins nothing is attached to, which presents as a board that boots and
    // says nothing at all.
    ESP_LOGI(TAG, "panel %dx%d, board id %d", w, h, (int)M5.getBoard());

    // The core, exercised rather than merely linked. schedule_weekday is pure
    // arithmetic pinned against real dates in the host tests, so a wrong answer
    // here means the toolchain is miscompiling it, not that the logic drifted.
    // 20692 is 2026-08-27, a Thursday, which is 4 with Sunday as 0.
    const int wd = schedule_weekday(20692);
    ESP_LOGI(TAG, "core linked: schedule_weekday(20692) = %d (expect 4)", wd);

    daily_layout_t L;
    daily_flags_t f = { false, false, false, false };
    daily_layout(&f, &L);
    // Layout and panel must agree. They disagreed once, silently, because the
    // geometry was built for the product name's 600x400 rather than the
    // hardware's 400x600.
    ESP_LOGI(TAG, "daily_layout riddle_top=%d, canvas %dx%d, panel %dx%d%s",
             L.riddle_top, DL_CANVAS_W, DL_CANVAS_H, w, h,
             (w == DL_CANVAS_W && h == DL_CANVAS_H) ? "" : "  <-- MISMATCH");

    M5.Display.setRotation(0);

    // Sample content. NVS, the SD card and the network are not wired yet, so
    // the page is driven from literals -- which is the point of it taking its
    // content as a parameter. Every zone is populated so all of them draw at
    // once; real days will show fewer.
    static schedule_t sched;
    schedule_parse("{\"days\":{\"thu\":[\"\xd7\x9e\xd7\xaa\xd7\x9e\xd7\x98\xd7\x99\xd7\xa7\xd7\x94\","
                   "\"\xd7\xa1\xd7\xa4\xd7\x95\xd7\xa8\xd7\x98\"]}}", &sched);

    static weather_t wx;
    wx.temp_x10 = 274; wx.hi_x10 = 310; wx.lo_x10 = 220;
    wx.wmo = 0; wx.fetched_at = 1;      // ancient, so the "old" marker shows

    page_daily_content c = {};
    c.date       = "27/08";
    c.streak     = 4;
    c.sched      = &sched;
    c.wx         = &wx;
    c.kids       = nullptr;             // no kids.json yet: those zones stay away
    c.today      = 20692;               // 2026-08-27, a Thursday
    c.month      = 8; c.day = 27;
    c.now_utc    = 1788000000u;
    c.question   = "\xd7\x9e\xd7\x94 \xd7\xa2\xd7\x95\xd7\x9c\xd7\x94 "
                   "\xd7\x95\xd7\x9c\xd7\x90 \xd7\x99\xd7\x95\xd7\xa8\xd7\x93";
    c.choices[0] = "\xd7\x92\xd7\x99\xd7\x9c";
    c.choices[1] = "\xd7\x92\xd7\xa9\xd7\x9d";
    c.choices[2] = "\xd7\xa9\xd7\x9e\xd7\xa9";
    c.has_choices = true;

    const int64_t t_draw = esp_timer_get_time();
    page_daily_draw(c);
    const int64_t push_ms = (esp_timer_get_time() - t_draw) / 1000;

    const int64_t t0 = esp_timer_get_time();
    M5.Display.display();
    const int64_t ms = (esp_timer_get_time() - t0) / 1000;

    ESP_LOGI(TAG, "FULL REFRESH: draw+push %lld ms, trailing display() %lld ms",
             (long long)push_ms, (long long)ms);

    // The clock, then the alarm. Arming is verified by readback inside
    // wake_arm_next -- a board that thinks it is armed and is not never wakes
    // again and looks exactly like a working one.
    wake_sync_clock();
    int morning = 0;
    if (!wake_arm_next(time(nullptr), &morning))
        ESP_LOGE(TAG, "ALARM DID NOT ARM -- this board will not wake on its own");

    // No grace period any more: wake_sleep_if_safe() refuses outright while
    // USB is attached, which is a rule rather than a race. A timed window was
    // the wrong shape -- it made every reflash a stopwatch exercise.
    if (!wake_sleep_if_safe())
        ESP_LOGW(TAG, "staying awake; the page is drawn and the alarm is armed");
}
