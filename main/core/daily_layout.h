// Morning Riddle: where each zone of the daily page goes. M5Paper Color.
//
// IDF-free and drawing-free on purpose. Three optional zones (schedule,
// weather, callout) plus the birthday banner give sixteen combinations.
// Checking those on the board means contriving a birthday, an empty weekend
// timetable and a failed weather fetch, then waiting 15-30 SECONDS per redraw
// on a panel with no partial update. Reflow tangled into drawing code is
// reflow nobody verifies. (Eng review D8, and far more true here.)
//
// COORDINATES ARE THE PANEL, 600x400 LANDSCAPE.
// A zone that is not present gets y == DL_ABSENT and consumes no height, so
// everything below it moves up.

#ifndef DAILY_LAYOUT_H
#define DAILY_LAYOUT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DL_ABSENT      (-1)

// 600x400, versus the Waveshare board's 480x800 portrait. Wider by 120,
// and SHORTER BY HALF. That inversion is what drove the rest of this file:
// vertical space is now the scarce resource and width is the plentiful one.
#define DL_CANVAS_W    600
#define DL_CANVAS_H    400
#define DL_MARGIN_X     14
#define DL_BODY_BOTTOM (DL_CANVAS_H - 8)

#define DL_HDR_Y         8
#define DL_HDR_RULE_Y   44
#define DL_LINE_H       41      // one Hebrew text line
#define DL_BAND_PAD      6
#define DL_ZONE_GAP      6

// THE BAND IS HORIZONTAL HERE, NOT STACKED.
//
// On the portrait board the schedule sat above the weather, two lines costing
// 82px out of 800. Spending 82 of 400 on utility would leave the riddle less
// than half the page. Side by side they cost one line instead of two, and the
// 600px width easily carries both.
#define DL_BAND_SPLIT_X 330     // schedule left of this, weather right of it

// The riddle needs a wrapped question plus three choices: two lines at 41
// (82) and three choices at ~44 (132) is 214. 200 is the floor, and unlike
// the portrait board's 420 it is genuinely TIGHT -- the worst case lands at
// 220, twenty pixels of slack. That is the honest number for this panel, not
// a comfortable one, and it is why the birthday rule below exists.
#define DL_RIDDLE_MIN_H 200

// A BIRTHDAY SUPPRESSES THE CALLOUT.
//
// Both zones do the same job -- address the reader by name -- and on a 400px
// page the pair costs 110px, a quarter of everything below the header. The
// banner already makes the screen unmistakably about that child, so the
// callout adds nothing but crowding. On the 800px board they could coexist;
// here they cannot, and choosing which to drop is a layout decision rather
// than something to discover as a clipped riddle.
#define DL_BIRTHDAY_SUPPRESSES_CALLOUT 1

// NO APPROACH-C FLOOR.
//
// The portrait design pinned the riddle into the lower two-thirds so it sat at
// a child's eye level while the utility band sat at an adult's. That trick
// needed 800px of height to separate the two. At 400px the whole page is one
// glance -- there is no "lower two-thirds" to aim at, and forcing one would
// only steal space from the riddle. The mounting height question that governed
// DL_RIDDLE_TOP_MIN does not arise on this panel.

typedef struct {
    bool schedule;      // today has subjects (weekends usually do not)
    bool weather;       // a cached reading exists, stale or not
    bool callout;       // fires about one day in three
    bool birthday;      // rare; a banner, not a takeover
} daily_flags_t;

typedef struct {
    int band_y, band_h;     // one horizontal band; band_h 0 when empty
    int schedule_x, schedule_y;
    int weather_x, weather_y;
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
