// Morning Riddle: daily page reflow, 400x600 portrait. See the header for why.
//
// THE SHAPE IS A NEWSPAPER'S, and that is a layout decision, not a decoration
// one. A masthead line and a thick rule; the day's facts between hairlines;
// anything addressed to the reader below that; a rule; then the lead. Rules do
// the work a box used to: a bordered rectangle around the timetable made it
// look like a form to fill in, where two hairlines make it look like a column
// to read.

#include "daily_layout.h"

void daily_layout(const daily_flags_t *f, daily_layout_t *out)
{
    if (!out) return;

    daily_flags_t none = { false, false, false, false };
    if (!f) f = &none;

    out->band_y           = DL_HDR_RULE_Y + DL_HDR_RULE_H + DL_ZONE_GAP;
    out->band_h           = 0;
    out->schedule_y       = DL_ABSENT;
    out->weather_y        = DL_ABSENT;
    out->birthday_label_y = DL_ABSENT;
    out->birthday_y       = DL_ABSENT;
    out->callout_y        = DL_ABSENT;

    // Schedule above weather, stacked. Side by side would give each 190px on a
    // 400-wide panel, which is not enough for a Hebrew timetable line.
    int y = out->band_y + DL_HAIR + DL_BAND_PAD;
    if (f->schedule) { out->schedule_y = y; y += DL_LINE_H; }
    if (f->weather)  { out->weather_y  = y; y += DL_LINE_H; }

    // The band is two hairlines, so it exists only when it has contents -- a
    // pair of rules with nothing between them on a weekend morning reads as a
    // fault, not as a design.
    if (out->schedule_y != DL_ABSENT || out->weather_y != DL_ABSENT) {
        y += DL_BAND_PAD;
        out->band_h = (y + DL_HAIR) - out->band_y;
        y = out->band_y + out->band_h + DL_ZONE_GAP;
    } else {
        y = out->band_y;
    }

    // Birthday and callout sit outside the band: both address the reader
    // rather than inform them, and ruling that would make it look like data.
    // They coexist. The landscape version had the birthday suppress the
    // callout, but that was bought with space this panel turns out to have.
    //
    // The birthday is a small standing head over the name rather than two body
    // lines. It costs 17px less, and it is the right relationship: the label
    // is the same every year and the name is the news.
    if (f->birthday) {
        out->birthday_label_y = y;
        y += DL_SMALL_H + 2;
        out->birthday_y = y;
        y += DL_LINE_H + DL_ZONE_GAP;
    }
    if (f->callout) {
        out->callout_y = y;
        y += DL_LINE_H + DL_ZONE_GAP;
    }

    // The rule above the lead, always drawn. It is what makes the riddle read
    // as the story rather than as one more line of the same list.
    if (y < DL_RIDDLE_TOP_MIN) y = DL_RIDDLE_TOP_MIN;
    out->lead_rule_y = y;
    y += DL_LEAD_RULE_H + 10;

    out->riddle_top = y;
    out->riddle_h   = DL_BODY_BOTTOM - y;
}
