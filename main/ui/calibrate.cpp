#include "calibrate.hpp"

#include <M5Unified.h>
#include <cstdio>

#include "esp_log.h"

#include "gfx_target.hpp"

static const char *TAG = "calib";

extern "C" {
#include "daily_layout.h"
}

void calibrate_draw()
{
    auto &g = ui_canvas();
    g.startWrite();
    g.fillScreen(TFT_WHITE);

    // Sixteen bands, one per LUT step. The label sits on white so it stays
    // readable whatever the swatch turns into -- the whole point is that we do
    // not yet know what the swatch turns into.
    const int rows = 16;
    const int h    = DL_CANVAS_H / rows;          // 37
    const int x0   = 64;

    g.setTextColor(TFT_BLACK);
    for (int i = 0; i < rows; i++) {
        const int y = i * h;

        // The 8-bit grey the driver reduces to LUT step i. Centre of the
        // bucket, not its edge, so rounding cannot land us one step over.
        const uint8_t v = (uint8_t)(i * 17);
        const uint32_t c = g.color888(v, v, v);
        g.fillRect(x0, y, DL_CANVAS_W - x0 - 4, h - 2, c);

        // Index, and the 8-bit value that produced it, so the photograph is
        // self-describing and nobody has to count rows.
        char lbl[16];
        std::snprintf(lbl, sizeof lbl, "%2d", i);
        g.setTextSize(3);
        g.drawString(lbl, 6, y + 6);
        std::snprintf(lbl, sizeof lbl, "%3d", (int)v);
        g.setTextSize(1);
        g.drawString(lbl, 8, y + 26);
    }

    g.endWrite();

    // READ THE BUFFER BACK BEFORE IT IS PUSHED.
    //
    // The panel came back a uniform dark field, which has two completely
    // different causes: the canvas holds the wrong pixels, or the canvas is
    // right and what reaches the panel is not. A photograph cannot tell those
    // apart and neither can I. Sampling the sprite at known points does, in
    // one line of log: if band 15 reads near 255 and band 0 near 0, the buffer
    // is correct and the fault is downstream of it.
    ESP_LOGW(TAG, "canvas depth=%d  sample: margin=%06lx b0=%06lx b8=%06lx b15=%06lx",
             (int)g.getColorDepth(),
             (unsigned long)g.readPixel(4, 4),
             (unsigned long)g.readPixel(x0 + 40, 0 * h + 8),
             (unsigned long)g.readPixel(x0 + 40, 8 * h + 8),
             (unsigned long)g.readPixel(x0 + 40, 15 * h + 8));
}
