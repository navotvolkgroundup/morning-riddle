// Morning Riddle: daily page reflow, 600x400. See daily_layout.h for the why.

#include "daily_layout.h"

void daily_layout(const daily_flags_t *f, daily_layout_t *out)
{
    if (!out) return;

    daily_flags_t none = { false, false, false, false };
    if (!f) f = &none;

    out->band_y      = DL_HDR_RULE_Y + DL_ZONE_GAP;
    out->band_h      = 0;
    out->schedule_x  = DL_ABSENT;
    out->schedule_y  = DL_ABSENT;
    out->weather_x   = DL_ABSENT;
    out->weather_y   = DL_ABSENT;
    out->birthday_y  = DL_ABSENT;
    out->callout_y   = DL_ABSENT;

    // Schedule and weather share ONE line, side by side. The band draws a
    // border, so it only exists when it has something in it -- an empty box on
    // a weekend morning reads as a fault, not as a design.
    const int inner_y = out->band_y + DL_BAND_PAD;
    if (f->schedule) {
        out->schedule_x = DL_MARGIN_X + DL_BAND_PAD;
        out->schedule_y = inner_y;
    }
    if (f->weather) {
        out->weather_x = DL_BAND_SPLIT_X;
        out->weather_y = inner_y;
    }

    int y;
    if (f->schedule || f->weather) {
        out->band_h = DL_BAND_PAD + DL_LINE_H + DL_BAND_PAD;
        y = out->band_y + out->band_h + DL_ZONE_GAP;
    } else {
        y = out->band_y;                    // no band drawn, no space taken
    }

    // Birthday and callout sit outside the band: both address the reader
    // rather than inform them, and boxing that would make it look like data.
    // ONE line inside the border, not two. The portrait banner spent two
    // lines on "happy birthday" plus the name; at 400px that is 98px, and the
    // name alone carries the message.
    if (f->birthday) {
        out->birthday_y = y;
        y += DL_LINE_H + 16 + DL_ZONE_GAP;
    }
    // Suppressed on a birthday -- see DL_BIRTHDAY_SUPPRESSES_CALLOUT. Both
    // address the reader, and the page cannot afford to say it twice.
    if (f->callout && !f->birthday) {
        out->callout_y = y;
        y += DL_LINE_H + DL_ZONE_GAP;
    }

    out->riddle_top = y;
    out->riddle_h   = DL_BODY_BOTTOM - y;
}
