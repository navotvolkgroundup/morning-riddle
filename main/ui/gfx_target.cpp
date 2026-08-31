#include "gfx_target.hpp"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "gfx";

namespace {
M5Canvas *g_cv = nullptr;
bool      g_tried = false;

// Somewhere safe to draw when there is no canvas.
//
// The old fallback reinterpret_cast M5.Display to an M5Canvas& and handed that
// back. Both derive from LovyanGFX, so it compiles and mostly appears to work,
// and it is undefined behaviour: the vtables are not the same shape and a
// virtual call through the wrong one is a crash waiting for the right
// afternoon. A sprite with no buffer is the honest version -- every draw call
// checks _img and returns, so the page draws into nothing and says so, which
// is what the fallback was pretending to do anyway.
M5Canvas g_void;
}  // namespace

M5Canvas &ui_canvas()
{
    if (!g_tried) {
        g_tried = true;
        g_cv = new M5Canvas(&M5.Display);

        // PSRAM, AND IT NEVER WAS. The comment here said "PSRAM has 8MB; this
        // is a rounding error against that" and the code never asked for it --
        // LGFX_Sprite::_psram defaults to false, so 400x600 at the panel's
        // grayscale_8bit depth is 240KB taken out of internal DMA RAM, which
        // on an ESP32-S3 with the WiFi stack up is most of what is left.
        //
        // It succeeded for weeks and then did not, on a boot that had just
        // fetched a batch and a picture, and the page silently did not draw:
        // "no canvas; nothing pushed". Intermittent, allocation-order
        // dependent, and invisible unless you read the log. There are 8MB of
        // octal PSRAM on this board and this belongs in them.
        g_cv->setPsram(true);

        if (!g_cv->createSprite(M5.Display.width(), M5.Display.height())) {
            // Say how much of what was free, so the next failure is a fact
            // rather than a mystery.
            ESP_LOGE(TAG, "createSprite failed: %u free PSRAM, %u free internal",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            delete g_cv;
            g_cv = nullptr;
        } else {
            ESP_LOGI(TAG, "canvas %dx%d in PSRAM, %u free after",
                     M5.Display.width(), M5.Display.height(),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }
    }
    // Never hand back a null reference: drawing code stays total. Nothing
    // appears, and nothing crashes.
    return g_cv ? *g_cv : g_void;
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
