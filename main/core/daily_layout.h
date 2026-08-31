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
#define DL_LINE_H       41      // one body line
#define DL_SMALL_H      24      // one small-face line (dateline, labels)
#define DL_BAND_PAD      4
#define DL_ZONE_GAP      7

// THE MASTHEAD RULE. Thick, because it is the one division on the page that
// separates the paper's identity from the paper's contents. Every other rule
// here is a hairline; the lead gets 2px. Three weights of rule is how a page
// says "major, minor, minor" without a second type size.
// THE MASTHEAD IS A NAMEPLATE AND A REVERSED BAR, not a line and a rule.
//
// The nameplate had the dateline beside it on one line and a 3px rule under
// both, which is a heading, not a masthead. A paper puts its name alone and
// then a strip of furniture under it, and reversing that strip -- white type
// on solid black -- is the single strongest device available on a panel whose
// black is properly black. It also solved the collision: the name no longer
// shares a line with anything.
#define DL_BAR_Y        44      // top of the reversed dateline bar
#define DL_BAR_H        28
#define DL_HDR_RULE_Y   DL_BAR_Y            // kept: "below the masthead"
#define DL_HDR_RULE_H   DL_BAR_H
#define DL_HAIR          1
#define DL_LEAD_RULE_H   2

// The boxed weather panel: symbol, temperature, advice. The timetable wraps
// beside it in what is left, which is what makes the band read as a page
// rather than as two stacked captions.
#define DL_WXBOX_W     126
#define DL_WXBOX_H     104
#define DL_SCHED_LINES   2

// A wrapped question (two lines, 82) plus three choices (~64 each, 192) is
// 274. The worst case -- every zone present -- leaves 307, so this fires
// before a riddle clips rather than after someone notices one did.
#define DL_RIDDLE_MIN_H 280

// ZERO, AND THE HEADER ALREADY SAID THIS WAS A CONSTANT CHANGE IF WRONG.
//
// This was 200: a floor that weighted the riddle low so the utility band sat
// at an adult's gaze and the riddle at a child's. The floor is now doing harm
// rather than good, for two reasons.
//
// The page draws a rule above the lead. With a floor, a sparse day put that
// rule 50px below the band with nothing in between -- an empty ruled band,
// which on a page that is trying to look like a newspaper reads as a story
// that failed to load, not as breathing room.
//
// And the goal survives without it: page_daily now measures the riddle block
// and centres it in whatever space is left, so on a sparse day the riddle
// lands mid-panel anyway. Centring does what the floor was for, and does it
// from the block's real height instead of a guess.
#define DL_RIDDLE_TOP_MIN 0

typedef struct {
    bool schedule;      // today has subjects (weekends usually do not)
    bool weather;       // a cached reading exists, stale or not
    bool callout;       // whose turn it is; every day there are kids
    bool birthday;      // rare; a banner, not a takeover
} daily_flags_t;

typedef struct {
    int band_y, band_h;     // hairline-ruled facts band; band_h 0 when empty
    int schedule_y;         // first of up to DL_SCHED_LINES wrapped lines
    int weather_y;          // top of the boxed panel, not a text baseline
    int birthday_label_y;   // "birthday" in small; DL_ABSENT with birthday_y
    int birthday_y;         // the name, in body
    int callout_y;
    int lead_rule_y;        // rule above the lead; always placed
    int riddle_top;         // first y the riddle may use
    int riddle_h;           // riddle_top .. DL_BODY_BOTTOM
} daily_layout_t;

// Always succeeds. Fields for absent zones are DL_ABSENT.
void daily_layout(const daily_flags_t *f, daily_layout_t *out);

// Where today's picture goes, or DL_ABSENT for "not today".
//
// THE PICTURE FILLS SPACE THE RIDDLE IS NOT USING; IT NEVER TAKES ANY. It used
// to be a zone under the masthead that pushed everything down, and it worked
// until the page became a newspaper: the nameplate, the reversed bar and the
// boxed weather panel between them took 49px, which left a normal school day
// 45px of picture -- a sliver, not an illustration, and a feature that would
// have quietly stopped firing.
//
// The riddle block is centred in the riddle zone, so on a short-riddle morning
// there is real slack doing nothing. The picture takes that and the block
// re-centres in what remains. On a long-riddle morning there is none and there
// is no picture, which is the right answer twice: a five-line riddle is
// already the interesting thing on the page.
//
// `block_h` is the measured height of the question, choices or answer -- the
// caller knows it, the layout cannot.
int daily_image_in_slack(int riddle_top, int riddle_h, int block_h, int image_h);

#ifdef __cplusplus
}
#endif

#endif // DAILY_LAYOUT_H
