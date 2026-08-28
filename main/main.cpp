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
#include "feedback.hpp"
#include "state.hpp"

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

    // CLOCK FIRST, before anything asks what day it is. riddle_local_day()
    // keys the whole state machine, so a page recorded against an unsynced
    // 1970 and a guess judged against the real date disagree -- and every
    // press is then correctly refused as "yesterday's screen", which looks
    // exactly like a broken button.
    wake_sync_clock();

    // THE BUTTON SHORT PATH. A guess is acknowledged by the LED and a chirp
    // and nothing else: the panel needs 17.1s and cannot answer a press, so
    // the reveal waits for the 16:00 wake. This costs well under a second.
    //
    // M5.begin() is called first now -- at 464ms it is affordable, and it is
    // what brings up the LED and the speaker. That was not true when
    // M5.begin() appeared to cost 52.7s, which is why this path used to skip
    // it entirely and had no way to acknowledge anything.
    if (why == wake_cause::button) {
        const int b = wake_button_index();

        riddle_nvs_t st;
        state_load(&st);

        riddle_input_t in = {};
        in.reason  = WAKE_GUESS;
        in.guess   = (int8_t)b;
        in.batch_n = 1;                 // the sample riddle; the real count
                                        // arrives with the fetched batch
        in.today   = riddle_local_day(time(nullptr), RIDDLE_TZ);

        const riddle_action_e act = riddle_decide(&in, &st);

        // ACKNOWLEDGE WHAT THE STATE MACHINE DECIDED, not what was pressed.
        // A guess after the answer is out, or against yesterday's screen, is a
        // no-op -- and telling a child their guess landed when it did not is
        // worse than telling them it did not.
        if (act == ACT_SHOW_RESULT) {
            if (!state_save(&st))
                ESP_LOGE(TAG, "GUESS NOT SAVED -- it will be forgotten by 16:00");
            ESP_LOGW(TAG, "guess %d accepted, streak %u", b, (unsigned)st.streak);
            feedback_guess(b);
        } else {
            ESP_LOGW(TAG, "guess %d refused (state=%u, day=%ld vs today=%ld)",
                     b, (unsigned)st.state, (long)st.day, (long)in.today);
            feedback_reject();
        }

        feedback_settle();
        wake_sleep();
    }
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

    // The page is up, so a guess is now legitimate. Without this the state
    // stays RS_IDLE and every press is correctly refused -- the screen would
    // show a riddle that the state machine does not believe exists.
    {
        riddle_nvs_t st;
        state_load(&st);
        riddle_input_t in = {};
        in.reason  = WAKE_MORNING;
        in.guess   = RIDDLE_NO_GUESS;
        in.batch_n = 1;
        in.today   = riddle_local_day(time(nullptr), RIDDLE_TZ);
        const riddle_action_e act = riddle_decide(&in, &st);
        ESP_LOGI(TAG, "page state: action=%d state=%u day=%ld",
                 (int)act, (unsigned)st.state, (long)st.day);
        state_save(&st);
    }

    // BRING-UP DEMO. On USB the board stays awake and never takes the button
    // path, so this is the only way to see and hear the feedback hardware.
    // Remove once guesses are wired to real presses.
    ESP_LOGW(TAG, "feedback demo: three guesses, then a rejection");
    for (int i = 0; i < 3; i++) { feedback_guess(i); feedback_settle(); }
    feedback_reject();
    feedback_settle();
    ESP_LOGW(TAG, "feedback demo done");

    // The alarm. Arming is verified by readback inside wake_arm_next -- a
    // board that thinks it is armed and is not never wakes again and looks
    // exactly like a working one. The clock was synced above.
    int morning = 0;
    if (!wake_arm_next(time(nullptr), &morning))
        ESP_LOGE(TAG, "ALARM DID NOT ARM -- this board will not wake on its own");

    // No grace period any more: wake_sleep_if_safe() refuses outright while
    // USB is attached, which is a rule rather than a race. A timed window was
    // the wrong shape -- it made every reflash a stopwatch exercise.
    if (!wake_sleep_if_safe())
        ESP_LOGW(TAG, "staying awake; the page is drawn and the alarm is armed");
}
