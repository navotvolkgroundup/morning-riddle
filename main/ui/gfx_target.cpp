#include "gfx_target.hpp"

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "gfx";

namespace {
M5Canvas *g_cv = nullptr;
bool      g_tried = false;
}  // namespace

M5Canvas &ui_canvas()
{
    if (!g_tried) {
        g_tried = true;
        g_cv = new M5Canvas(&M5.Display);
        // 400x600 at the panel's depth. PSRAM has 8MB; this is a rounding
        // error against that, and the alternative is not drawing at all.
        if (!g_cv->createSprite(M5.Display.width(), M5.Display.height())) {
            ESP_LOGE(TAG, "createSprite failed -- nothing can be drawn");
            delete g_cv;
            g_cv = nullptr;
        }
    }
    // Never hand back a null reference: fall back to the panel so drawing code
    // stays total. It will not appear, but it will not crash either.
    return g_cv ? *g_cv : *reinterpret_cast<M5Canvas *>(&M5.Display);
}

bool ui_canvas_ready() { return g_cv != nullptr; }

int64_t ui_canvas_push()
{
    if (!g_cv) { ESP_LOGE(TAG, "no canvas; nothing pushed"); return -1; }
    // ASSERT THE MODE ON EVERY PUSH, not once at boot.
    //
    // Measured in one boot: the identical image pushed in 17138 ms at t=1.6s
    // and 1994 ms at t=42s, with the SD retries, WiFi and NTP in between. A
    // 2 s "refresh" on this panel moves no ink -- its own init takes 52 s and
    // the datasheet says 15-30 s -- so something in that window puts the panel
    // back into a fast mode. Setting it here costs one I2C-free register write
    // and removes the dependency on nothing else having touched it.
    M5.Display.setEpdMode(m5gfx::epd_mode_t::epd_quality);

    const int64_t t = esp_timer_get_time();
    g_cv->pushSprite(0, 0);
    M5.Display.waitDisplay();
    return (esp_timer_get_time() - t) / 1000;
}
