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

    // SIDE BY SIDE NOW, which an earlier comment here said was impossible:
    // "side by side would give each 190px on a 400-wide panel, which is not
    // enough for a Hebrew timetable line." True of two TEXT columns. The
    // weather is a 126px panel rather than a line, which leaves the timetable
    // 232px and two lines to wrap into -- more room in total than the single
    // line it used to get, and the band is 104px instead of 98 for both.
    int y = out->band_y + DL_HAIR + DL_BAND_PAD;
    const int box = f->weather ? DL_WXBOX_H : 0;
    if (f->weather)  out->weather_y  = y;
    if (f->schedule) out->schedule_y = y + 8;   // optical, against the box top
    if (f->schedule || f->weather) {
        // The band is as tall as the panel when there is one, and one wrapped
        // timetable otherwise.
        y += box ? box : (DL_SCHED_LINES * (DL_LINE_H - 6));
    }

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
    // A BIRTHDAY REPLACES THE TURN LINE RATHER THAN STACKING ON IT. Both
    // address the reader by name, and on the one morning of the year that a
    // child's name is on the page in red, telling a different child that
    // today's riddle is for them is the wrong page. It also buys back the 47px
    // that made the crowded day the case nothing else could fit into.
    if (f->birthday) {
        out->birthday_label_y = y;
        y += DL_SMALL_H + 2;
        out->birthday_y = y;
        y += DL_LINE_H + DL_ZONE_GAP;
    } else if (f->callout) {
        out->callout_y = y;
        y += DL_LINE_H + DL_ZONE_GAP;
    }

    // The rule above the lead, always drawn. It is what makes the riddle read
    // as the story rather than as one more line of the same list.
    if (y < DL_RIDDLE_TOP_MIN) y = DL_RIDDLE_TOP_MIN;
    out->lead_rule_y = y;
    y += DL_LEAD_RULE_H + 10;

    out->riddle_top = y;
    // The folio and its gap are not the riddle's to use.
    out->riddle_h   = (DL_FOLIO_Y - DL_ZONE_GAP) - y;


}

int daily_image_in_slack(int riddle_top, int riddle_h, int block_h, int image_h)
{
    if (image_h <= 0) return DL_ABSENT;

    // What the block is not using. DL_ZONE_GAP twice: once above the picture
    // and once below it, so it never touches the lead rule or the question.
    const int slack = riddle_h - block_h;
    if (slack < image_h + 2 * DL_ZONE_GAP) return DL_ABSENT;

    // Directly under the lead rule, which is where a paper puts a picture that
    // belongs to the story beneath it.
    return riddle_top + DL_ZONE_GAP;
}
