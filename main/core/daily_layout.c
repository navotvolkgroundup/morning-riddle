// Morning Riddle: daily page reflow, 400x600 portrait. See the header for why.

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

    // Schedule above weather, stacked. Side by side would give each 190px on a
    // 400-wide panel, which is not enough for a Hebrew timetable line.
    int y = out->band_y + DL_BAND_PAD;
    if (f->schedule) { out->schedule_y = y; y += DL_LINE_H; }
    if (f->weather)  { out->weather_y  = y; y += DL_LINE_H; }

    // The band draws a border, so it exists only when it has contents -- an
    // empty box on a weekend morning reads as a fault, not as a design.
    if (out->schedule_y != DL_ABSENT || out->weather_y != DL_ABSENT) {
        out->band_h = (y + DL_BAND_PAD) - out->band_y;
        y = out->band_y + out->band_h + DL_ZONE_GAP;
    } else {
        y = out->band_y;
    }

    // Birthday and callout sit outside the band: both address the reader
    // rather than inform them, and boxing that would make it look like data.
    // They coexist. The landscape version had the birthday suppress the
    // callout, but that was bought with space this panel turns out to have.
    if (f->birthday) {
        out->birthday_y = y;
        y += 2 * DL_LINE_H + 16 + DL_ZONE_GAP;
    }
    if (f->callout) {
        out->callout_y = y;
        y += DL_LINE_H + DL_ZONE_GAP;
    }

    // Never above the eye-level line, never overlapping a zone: whichever is
    // lower wins, so a full page still pushes down.
    if (y < DL_RIDDLE_TOP_MIN) y = DL_RIDDLE_TOP_MIN;

    out->riddle_top = y;
    out->riddle_h   = DL_BODY_BOTTOM - y;
}
