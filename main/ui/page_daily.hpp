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
#include "schedule.h"
#include "weather.h"
}

// Which physical edge the three buttons sit on. The choice markers must be on
// the same side as the buttons they name, or the page silently instructs the
// reader to press the wrong one.
//
// UNVERIFIED. On the Waveshare board this was guessed wrong first and only
// settled by looking at the hardware. Check it here before anyone relies on it.
#define PD_BUTTONS_ON_LEFT_EDGE 1

struct page_daily_content {
    const char     *date;           // short date, ASCII, e.g. "27/08"
    uint32_t        streak;         // days; hidden below 2
    const schedule_t *sched;        // may be null
    const weather_t  *wx;           // may be null; stale is still shown
    const kids_t     *kids;         // may be null
    int32_t         today;          // civil day number, for schedule + callout
    int             month, day;     // for the birthday check
    uint32_t        now_utc;        // for the weather staleness marker

    const char     *question;       // Hebrew, wrapped
    const char     *choices[3];     // Hebrew; ignored unless has_choices
    bool            has_choices;
};

// Draws the complete page into the framebuffer and pushes it once.
//
// ONE push. Every primitive can trigger its own full-panel waveform otherwise,
// and a waveform is 17 seconds -- a page drawn unbatched takes minutes and
// looks like a hang.
void page_daily_draw(const page_daily_content &c);

#endif // UI_PAGE_DAILY_HPP
