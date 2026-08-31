#include "calibrate.hpp"

#include <M5Unified.h>
#include <cstdio>

#include "gfx_target.hpp"

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
}
