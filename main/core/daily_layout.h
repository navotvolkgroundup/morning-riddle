// Morning Riddle: where each zone of the daily page goes.
//
// IDF-free and drawing-free on purpose. Three optional zones (schedule,
// weather, callout) give eight combinations, and the birthday banner doubles
// that again. Checking those on the board means contriving eight different
// days -- a birthday, an empty weekend timetable, a failed weather fetch --
// and squinting at a panel that takes four seconds to refresh. Reflow tangled
// into drawing code is reflow nobody verifies. (Eng review D8.)
//
// Pure arithmetic instead: the renderer asks where things go, then draws.
//
// COORDINATES ARE THE ROTATED CANVAS, 480x800, matching page_riddle.cc.
// A zone that is not present gets y == DL_ABSENT and consumes no height, so
// everything below it moves up.

#ifndef DAILY_LAYOUT_H
#define DAILY_LAYOUT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DL_ABSENT      (-1)

#define DL_CANVAS_W    480
#define DL_CANVAS_H    800
#define DL_MARGIN_X     14
#define DL_BODY_BOTTOM (DL_CANVAS_H - 8)

#define DL_HDR_Y        10
#define DL_HDR_RULE_Y   (DL_HDR_Y + 30)
#define DL_LINE_H       41      // HE_H in hebrew.inc; one Hebrew text line
#define DL_BAND_PAD      8      // inside the bordered utility band
#define DL_ZONE_GAP      6

// The riddle is the reason to walk over, so it gets a floor: if the utility
// zones ever grew past this, the page would become the noticeboard the design
// argued against. Nothing currently gets near it -- the assert exists to fail
// the day someone adds a fifth zone.
#define DL_RIDDLE_MIN_H 420

// APPROACH C, and the one number the wall decides.
//
// The design weights the riddle into the lower two-thirds because it assumes
// the board hangs at ADULT eye level: the utility band is then at a grown-up's
// natural gaze and the riddle at a child's. With few zones present the riddle
// would otherwise start around y=150 -- the upper fifth -- which is approach B
// wearing C's name.
//
// If the board ends up hung LOW, the advantage inverts and the adult is the
// one looking down. Set this to 0 in that case: the riddle then follows the
// utility zones directly and the layout becomes approach B. That is the whole
// change, which is what the design meant by "a constant change if wrong".
#define DL_RIDDLE_TOP_MIN 265           // 800/3, rounded

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
