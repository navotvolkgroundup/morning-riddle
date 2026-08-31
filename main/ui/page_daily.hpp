// The daily page: the whole thing, drawn once.
//
// Takes its content as a parameter rather than reading NVS, the SD card or the
// network. Two reasons. It can be drawn from sample data before any of those
// exist, which is how it gets looked at today; and when they do exist, the
// thing that decides WHAT to show stays separate from the thing that decides
// where it goes -- which is the split that has paid for itself twice already
// on this port.
//
// Zone geometry comes from core/daily_layout.c and is not re-derived here.

#ifndef UI_PAGE_DAILY_HPP
#define UI_PAGE_DAILY_HPP

#include <stdint.h>

extern "C" {
#include "daily_layout.h"
#include "kids.h"
#include "riddle_batch.h"
#include "schedule.h"
#include "weather.h"
}

// The three buttons are on the RIGHT edge -- verified on hardware 2026-08-28,
// having guessed left first and been wrong, exactly as on the Waveshare board.
// This used to drive PD_BUTTONS_ON_LEFT_EDGE, which placed A/B/C markers on the
// matching side. The markers are gone (they contradicted the case silkscreen;
// see page_daily.cpp), so nothing reads the edge any more. The fact is kept
// because the choices are ordered top-to-bottom to match those buttons, and
// that ordering is the page's only remaining affordance for which one to press.

struct page_daily_content {
    const char     *date;           // short date, ASCII, e.g. "27/08"
    uint32_t        issue;          // mornings published; hidden below 2
    int             turn_kid;       // whose turn today, or -1
    uint32_t        turn_streak;    // that kid's consecutive turns; hidden below 2
    const schedule_t *sched;        // may be null
    const weather_t  *wx;           // may be null; stale is still shown
    const kids_t     *kids;         // may be null
    int32_t         today;          // civil day number, for schedule + callout
    int             month, day;     // for the birthday check
    uint32_t        now_utc;        // for the weather staleness marker

    const char     *question;       // Hebrew, wrapped
    const char     *choices[3];     // Hebrew; ignored unless has_choices
    bool            has_choices;

    // The 13:00 reveal. When set, the answer replaces the choices: a child
    // reading the page after school wants the answer, and leaving three
    // unpressable options under it invites another guess the board will
    // refuse.
    const char     *answer;
    bool            show_answer;

    // Why the answer is the answer. Optional; an empty or null `why` draws the
    // reveal exactly as it did before. It is the difference between a riddle
    // that teaches something and a quiz a child got wrong.
    const char     *why;

    // What kind of thing today is (riddle_kind_e). Draws a small standing head
    // above the lead so a joke is not read as a riddle nobody can solve.
    uint8_t         kind;
};

// Draws the complete page into the framebuffer and pushes it once.
//
// ONE push. Every primitive can trigger its own full-panel waveform otherwise,
// and a waveform is ~17 seconds -- a page drawn unbatched takes hours and
// looks like a hang.
void page_daily_draw(const page_daily_content &c);

#endif // UI_PAGE_DAILY_HPP
