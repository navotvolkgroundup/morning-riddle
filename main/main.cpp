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
#include "sdcard.hpp"
#include "net.hpp"
#include "portal.hpp"
#include "batch.hpp"
#include "sdconfig.hpp"
#include "wx.hpp"

static const char *TAG = "riddle";

static riddle_batch_t s_batch;

extern "C" void app_main(void)
{
    // Logged BEFORE anything else, so a silent board can be told apart from a
    // board that never reached app_main. Everything about this port's bring-up
    // has been guesswork for want of exactly this line.
    ESP_LOGW(TAG, "app_main entered");

    // WAKE CAUSE BEFORE M5.begin(), always. The short path decides whether to
    // bring the display up at all, so it has to run before anything does.
    // (M5.begin() is 464ms here, not the 52.7s this comment used to claim.)
    const wake_cause why = wake_why();
    ESP_LOGW(TAG, "wake cause = %d, button = %d", (int)why, wake_button_index());

    auto cfg = M5.config();
    // Pin the board instead of trusting auto-detection. If detection fails the
    // display is never initialised and nothing draws -- silently, which is the
    // symptom we have.
    cfg.fallback_board = m5::board_t::board_M5PaperColor;
    // The page fills the screen itself, so M5's own clear is a wasted
    // full-panel waveform. This one line is what took M5.begin() from 52.7s to
    // 464ms -- the clear was almost all of it.
    cfg.clear_display = false;
    const int64_t t_begin = esp_timer_get_time();
    M5.begin(cfg);
    ESP_LOGW(TAG, "M5.begin took %lld ms (clear_display=false)",
             (long long)((esp_timer_get_time() - t_begin) / 1000));

    // RELEASE THE AUDIO STRAPPING PINS IMMEDIATELY unless we are about to
    // chirp. M5.begin() drives G45 and G46 high for the codec, and both are
    // boot strapping pins -- leaving them driven means the NEXT reset lands in
    // DOWNLOAD mode instead of running this application.
    //
    // The page path never makes a sound, so it has no reason to hold them for
    // even a moment. The button path needs the speaker and releases them in
    // feedback_settle() once the chirp has finished.
    //
    // This line is here because its absence cost most of a day: the fix
    // existed, on the button path only, so every ordinary boot armed the trap
    // and the board would draw its page once and then never boot again.
    if (why != wake_cause::button) feedback_release_straps();

    // The kids and the timetable, cache first then the card.
    //
    // BEFORE the radio, deliberately. The card mounted reliably when this
    // ran early and stopped once it moved after WiFi had been started and
    // stopped -- the SD power rail is switched by the PM1, and bringing the
    // radio up and down around it is the one thing that changed. Reading
    // config before using the network is the right order anyway.
    //
    // It is also independent of where WiFi came from: these were previously
    // only reached when credentials were missing, so a board configured
    // through the portal never touched its card at all.
    static kids_t     kids;
    static schedule_t sched;
    sdconfig_load(&kids, &sched);

    static weather_t wx;
    wx_load(&wx);               // cache only; the fetch is morning-only

    // NVS BEFORE THE NETWORK. esp_wifi_init() fails with
    // ESP_ERR_NVS_NOT_INITIALIZED and then abort()s, and the WiFi stack is the
    // first thing here that touches NVS. It used to be initialised lazily on
    // the first state load -- which happens after this block, so the portal
    // boot-looped the board.
    state_nvs_init();

    // SETUP ON DEMAND: hold a button and press reset.
    //
    // The names and the timetable were meant to arrive on the SD card. This
    // board's reader has never completed a data-block transfer, so without a
    // gesture there is no way to get them onto a device on a wall at all. It
    // has to be a COLD boot with a button held -- a button WAKE is a guess,
    // and turning a child's answer into a setup screen would be a bad joke.
    if (why == wake_cause::cold && wake_button_held()) {
        ESP_LOGW(TAG, "button held at reset -- opening setup");
        M5.Display.startWrite();
        M5.Display.fillScreen(TFT_WHITE);
        M5.Display.setTextColor(TFT_BLACK);
        M5.Display.setTextSize(2);
        M5.Display.drawString("Setup", 16, 40);
        M5.Display.drawString("Join this network:", 16, 100);
        M5.Display.setTextColor(TFT_RED);
        M5.Display.drawString(PORTAL_AP_SSID, 16, 140);
        M5.Display.setTextColor(TFT_BLACK);
        M5.Display.drawString("then open", 16, 200);
        M5.Display.drawString("192.168.4.1", 16, 240);
        M5.Display.endWrite();
        M5.Display.display();

        // kids and sched are updated in place, so the page below draws what
        // was just typed rather than what was loaded a moment ago.
        portal_run(&kids, &sched);
    }

    // Network first, so the clock below is the real one. All three steps are
    // allowed to fail: no card, no credentials, no network. The page still
    // draws from cache -- weather goes stale and the date may be wrong, which
    // is visibly degraded rather than blank.
    if (why != wake_cause::button) {
        if (!net_connect()) {
            // No network. Put the setup instructions on the panel -- a board
            // that silently fails to connect is indistinguishable from one
            // that is simply slow, and there is nowhere else to say it.
            M5.Display.startWrite();
            M5.Display.fillScreen(TFT_WHITE);
            M5.Display.setTextColor(TFT_BLACK);
            M5.Display.setTextSize(2);
            M5.Display.drawString("WiFi setup", 16, 40);
            M5.Display.drawString("Join this network:", 16, 100);
            M5.Display.setTextColor(TFT_RED);
            M5.Display.drawString(PORTAL_AP_SSID, 16, 140);
            M5.Display.setTextColor(TFT_BLACK);
            M5.Display.drawString("then open", 16, 200);
            M5.Display.drawString("192.168.4.1", 16, 240);
            M5.Display.endWrite();
            M5.Display.display();

            if (portal_run(&kids, &sched)) {
                // Straight back round: the credentials are in NVS now.
                if (net_connect()) { net_sync_time(); net_stop(); }
            }
        } else {
            net_sync_time();
            // Fetch while the radio is still up. Cache first: a network that
            // is merely slow must not cost the morning's riddle.
            static riddle_batch_t fetched;
            if (batch_fetch(&fetched) > 0) s_batch = fetched;
            wx_fetch(&wx, (uint32_t)time(nullptr));
            net_stop();
        }
    }

    // CLOCK FIRST, before anything asks what day it is. riddle_local_day()
    // keys the whole state machine, so a page recorded against an unsynced
    // 1970 and a guess judged against the real date disagree -- and every
    // press is then correctly refused as "yesterday's screen", which looks
    // exactly like a broken button.
    wake_sync_clock();



    // THE BUTTON SHORT PATH. A guess is acknowledged by the LED and a chirp
    // and nothing else: the answer is withheld until the 13:00 wake, so a
    // redraw could only repaint the same question. This costs well under a
    // second.
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
                ESP_LOGE(TAG, "GUESS NOT SAVED -- it will be forgotten by 13:00");
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
    // Real riddles if we have any, cached or freshly fetched.
    if (s_batch.count == 0) batch_load(&s_batch);

    // Ask the state machine what to show. It owns riddle selection: idx
    // advances once per day and wraps at the batch size, with a double-fire
    // guard so a second wake on the same day redraws rather than consuming
    // another riddle.
    static riddle_nvs_t st;
    state_load(&st);

    const time_t now_t = time(nullptr);
    struct tm lt;
    localtime_r(&now_t, &lt);

    riddle_input_t in = {};
    // An alarm wake knows which slot it is from the clock; anything else --
    // a cold boot, a reflash -- is treated as a morning, which is idempotent
    // within a day thanks to that guard.
    in.reason  = (why == wake_cause::alarm && lt.tm_hour >= RIDDLE_REVEAL_HOUR)
                     ? WAKE_AFTERNOON : WAKE_MORNING;
    in.guess   = RIDDLE_NO_GUESS;
    in.batch_n = (uint16_t)s_batch.count;      // the REAL count, not a stand-in
    in.today   = riddle_local_day(now_t, RIDDLE_TZ);

    const riddle_action_e act = riddle_decide(&in, &st);
    state_save(&st);
    ESP_LOGI(TAG, "action=%d idx=%u/%d state=%u streak=%u day=%ld",
             (int)act, (unsigned)st.idx, s_batch.count, (unsigned)st.state,
             (unsigned)st.streak, (long)st.day);


    page_daily_content c = {};
    static char datebuf[8];
    // The %% 100 is not paranoia about the calendar: without it the compiler
    // cannot prove tm_mday fits and -Werror=format-truncation fails the build.
    // Growing the buffer only moves the error, as the Waveshare build found.
    std::snprintf(datebuf, sizeof datebuf, "%02d/%02d",
                  lt.tm_mday % 100, (lt.tm_mon + 1) % 100);
    c.date       = datebuf;
    c.streak     = st.streak;
    c.sched      = &sched;
    c.wx         = (wx.fetched_at != 0) ? &wx : nullptr;
    c.kids       = (kids.count > 0) ? &kids : nullptr;
    c.today      = in.today;
    c.month      = lt.tm_mon + 1; c.day = lt.tm_mday;
    c.now_utc    = (uint32_t)now_t;
    if (s_batch.count > 0 && st.idx < s_batch.count) {
        const riddle_item_t &r = s_batch.item[st.idx];
        c.question    = r.q;
        c.choices[0]  = r.choices[0];
        c.choices[1]  = r.choices[1];
        c.choices[2]  = r.choices[2];
        c.has_choices = r.has_choices;
        c.answer      = r.a;
        c.show_answer = (act == ACT_SHOW_ANSWER);
        ESP_LOGI(TAG, "riddle %u of %d, choices=%d, reveal=%d",
                 (unsigned)st.idx, s_batch.count, (int)r.has_choices,
                 (int)c.show_answer);
    } else {
        // No batch at all: say so rather than drawing an empty page.
        c.question    = "\xd7\x90\xd7\x99\xd7\x9f \xd7\x97\xd7\x99\xd7\x93\xd7\x95\xd7\xaa";  // "no riddles"
        c.has_choices = false;
    }

    // ACT_NONE means what is on the panel is already correct. Skipping the
    // draw saves a 17-second full refresh and the power that goes with it --
    // on a panel this slow, not redrawing is a feature.
    if (act == ACT_NONE) {
        ESP_LOGI(TAG, "nothing to redraw; leaving the panel alone");
    }
    const int64_t t_draw = esp_timer_get_time();
    if (act != ACT_NONE) page_daily_draw(c);
    const int64_t push_ms = (esp_timer_get_time() - t_draw) / 1000;

    const int64_t t0 = esp_timer_get_time();
    M5.Display.display();
    const int64_t ms = (esp_timer_get_time() - t0) / 1000;

    ESP_LOGI(TAG, "FULL REFRESH: draw+push %lld ms, trailing display() %lld ms",
             (long long)push_ms, (long long)ms);

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
