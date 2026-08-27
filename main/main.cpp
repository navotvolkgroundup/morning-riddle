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

static const char *TAG = "riddle";

extern "C" void app_main(void)
{
    auto cfg = M5.config();
    M5.begin(cfg);

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
    ESP_LOGI(TAG, "daily_layout riddle_top=%d (PORTRAIT constants, wrong for "
                  "this panel -- geometry is the next task)", L.riddle_top);

    M5.Display.setRotation(0);
    M5.Display.fillScreen(TFT_WHITE);

    // Spectra 6 is black, white, red, yellow, blue, green. Drawing all six as
    // labelled bars is the cheapest way to find out what the panel actually
    // renders versus what M5GFX claims -- and how long a full refresh takes,
    // which is the number the whole interaction design rests on.
    struct { int c; const char *name; } bars[] = {
        { TFT_BLACK,  "black"  }, { TFT_RED,   "red"   },
        { TFT_YELLOW, "yellow" }, { TFT_BLUE,  "blue"  },
        { TFT_GREEN,  "green"  }, { TFT_WHITE, "white" },
    };
    const int n  = sizeof bars / sizeof bars[0];
    const int bw = w / n;
    for (int i = 0; i < n; i++) {
        M5.Display.fillRect(i * bw, h / 2, bw, h / 2 - 20, bars[i].c);
        M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
        M5.Display.setCursor(i * bw + 4, h - 18);
        M5.Display.print(bars[i].name);
    }

    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setTextSize(3);
    M5.Display.setCursor(12, 20);
    M5.Display.print("Morning Riddle");
    M5.Display.setTextSize(2);
    M5.Display.setCursor(12, 60);
    M5.Display.printf("%dx%d  core OK (wday=%d)", w, h, wd);

    const int64_t t0 = esp_timer_get_time();
    M5.Display.display();
    const int64_t ms = (esp_timer_get_time() - t0) / 1000;

    // The number that decides the interaction model. The design assumes a full
    // refresh is far too slow to answer a button press, and moved the reveal to
    // 16:00 because of it. Measure it rather than trusting the datasheet.
    ESP_LOGI(TAG, "FULL REFRESH TOOK %lld ms", (long long)ms);
}
