// Morning Riddle: where each zone of the daily page goes. M5Paper Color.
//
// IDF-free and drawing-free on purpose. Three optional zones (schedule,
// weather, callout) plus the birthday banner give sixteen combinations.
// Verifying those on the board means contriving a birthday, an empty weekend
// timetable and a failed weather fetch -- and this panel takes ~52s to
// initialise and 15-30s per redraw, with no partial update. Reflow tangled
// into drawing code is reflow nobody checks. (Eng review D8, more so here.)
//
// COORDINATES ARE THE PANEL, 400x600 PORTRAIT.
//
// The panel is NATIVELY 400x600. The "600x400" in the product name is the
// rotated view, and an earlier version of this file was built for it -- a
// horizontal band, a birthday that suppressed the callout, no eye-level floor,
// all justified by height having halved from the Waveshare board's 800 to 400.
// The hardware reports 600 of height. Those compensations were answers to a
// problem that did not exist, and are gone.

#ifndef DAILY_LAYOUT_H
#define DAILY_LAYOUT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DL_ABSENT      (-1)

#define DL_CANVAS_W    400
#define DL_CANVAS_H    600
#define DL_MARGIN_X     14
#define DL_BODY_BOTTOM (DL_CANVAS_H - 8)

#define DL_HDR_Y         8
#define DL_HDR_RULE_Y   40
#define DL_LINE_H       41      // one Hebrew text line
#define DL_BAND_PAD      8
#define DL_ZONE_GAP      6

// A wrapped question (two lines, 82) plus three choices (~64 each, 192) is
// 274. The worst case -- every zone present -- leaves 291, so this fires
// before a riddle clips rather than after someone notices one did.
#define DL_RIDDLE_MIN_H 280

// APPROACH C, and the one number the wall decides.
//
// The design weights the riddle low so the utility band sits at an adult's
// natural gaze and the riddle at a child's. With few zones present the riddle
// would otherwise start around y=46 and the separation disappears.
//
// 200 is a third of the panel, scaled from the Waveshare board's 265 of 800.
// If the board ends up hung LOW the advantage inverts, and the fix is to set
// this to 0: the riddle then follows the utility zones directly. That is the
// whole change -- which is what the design meant by "a constant change if
// wrong", and it is live again now that the page is portrait.
#define DL_RIDDLE_TOP_MIN 200

typedef struct {
    bool schedule;      // today has subjects (weekends usually do not)
    bool weather;       // a cached reading exists, stale or not
    bool callout;       // fires about one day in three
    bool birthday;      // rare; a banner, not a takeover
} daily_flags_t;

typedef struct {
    int band_y, band_h;     // bordered utility band; band_h 0 when empty
    int schedule_y;
    int weather_y;
    int birthday_y;
    int callout_y;
    int riddle_top;         // first y the riddle may use
    int riddle_h;           // riddle_top .. DL_BODY_BOTTOM
} daily_layout_t;

// Always succeeds. Fields for absent zones are DL_ABSENT.
void daily_layout(const daily_flags_t *f, daily_layout_t *out);

#ifdef __cplusplus
}
#endif

#endif // DAILY_LAYOUT_H
