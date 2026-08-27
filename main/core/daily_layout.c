// Morning Riddle: daily page reflow. See daily_layout.h for the why.

#include "daily_layout.h"

void daily_layout(const daily_flags_t *f, daily_layout_t *out)
{
    if (!out) return;

    daily_flags_t none = { false, false, false, false };
    if (!f) f = &none;

    out->band_y      = DL_HDR_RULE_Y + DL_ZONE_GAP;
    out->band_h      = 0;
    out->schedule_y  = DL_ABSENT;
    out->weather_y   = DL_ABSENT;
    out->birthday_y  = DL_ABSENT;
    out->callout_y   = DL_ABSENT;

    // The band holds schedule and weather. It draws a border, so it only
    // exists when it has something in it -- an empty box on a weekend morning
    // reads as a fault, not as a design.
    int y = out->band_y + DL_BAND_PAD;
    if (f->schedule) { out->schedule_y = y; y += DL_LINE_H; }
    if (f->weather)  { out->weather_y  = y; y += DL_LINE_H; }

    if (out->schedule_y != DL_ABSENT || out->weather_y != DL_ABSENT) {
        out->band_h = (y + DL_BAND_PAD) - out->band_y;
        y = out->band_y + out->band_h + DL_ZONE_GAP;
    } else {
        y = out->band_y;                    // no band drawn, no space taken
    }

    // Birthday and callout sit outside the band: both address the reader
    // rather than inform them, and boxing that would make it look like data.
    if (f->birthday) {
        out->birthday_y = y;
        y += 2 * DL_LINE_H + 16 + DL_ZONE_GAP;    // two lines inside a border
    }
    if (f->callout) {
        out->callout_y = y;
        y += DL_LINE_H + DL_ZONE_GAP;
    }

    // Never above the approach-C line, but never overlapping a zone either:
    // whichever is lower wins, so a page full of zones still pushes down.
    if (y < DL_RIDDLE_TOP_MIN) y = DL_RIDDLE_TOP_MIN;

    out->riddle_top = y;
    out->riddle_h   = DL_BODY_BOTTOM - y;
}
