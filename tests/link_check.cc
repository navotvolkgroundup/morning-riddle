// Linkage check: are the pure C units callable from C++?
//
// This file exists because of a bug that shipped past four clean firmware
// builds on 2026-08-26. page_riddle.cc is C++; riddle_decide.c, wake_log.c and
// kids.c are C. Their headers were missing `extern "C"` guards, so the C++ side
// emitted mangled references (_Z10kids_validPK6kids_t) that the C side never
// defines. Every reference was wrong, and nothing caught it.
//
// Nothing caught it because the firmware links with -ffunction-sections and
// --gc-sections, and until app_main calls page_riddle_show() the whole module
// is garbage-collected BEFORE the linker has to resolve anything. `idf.py
// build` reported success four times over a module that could never link. The
// failure would have surfaced later, as a wall of undefined references, at the
// moment someone wired up the dispatcher and had every reason to blame the
// wiring instead.
//
// So: compile this as C++, link it against the same C objects the firmware
// uses, and the mismatch becomes a link error in about a second. It takes the
// address of each function rather than calling it -- an address is enough to
// force resolution, and calling would mean inventing valid arguments.

#include "riddle_decide.h"
#include "wake_log.h"
#include "kids.h"
#include "weather.h"
#include "schedule.h"
#include "sd_json.h"
#include "daily_layout.h"
#include "he_text.h"
#include "riddle_batch.h"
#include "formdata.h"

#include <cstdio>

namespace {

// volatile + used so neither the optimiser nor --gc-sections can decide these
// references are pointless. That is not hypothetical caution: a plain
// `volatile void *probe[]` in main.cc was silently collected while I was
// chasing this, which is what sent the first two diagnoses the wrong way.
__attribute__((used)) void *const volatile kRefs[] = {
    (void *)riddle_local_day,
    (void *)riddle_next_wake,
    (void *)riddle_decide,

    (void *)wake_ring_push,
    (void *)wake_ring_read,
    (void *)wake_ring_valid,
    (void *)wake_ring_recent_guesses,
    (void *)wake_outcome_name,

    (void *)kids_valid,
    (void *)kids_birthday_on,
    (void *)kids_pick_callout,

    (void *)wmo_icon,
    (void *)wmo_label,
    (void *)weather_parse,
    (void *)weather_is_stale,

    (void *)schedule_weekday,
    (void *)schedule_parse,
    (void *)schedule_for_day,
    (void *)schedule_is_empty,

    (void *)sdj_read,
    (void *)sdj_strerror,

    (void *)daily_layout,

    (void *)he_utf8_next,
    (void *)he_glyph_width,
    (void *)he_advance,
    (void *)he_measure,
    (void *)he_line_break,

    (void *)riddle_batch_parse,
    (void *)form_field,
};

}  // namespace

int main(void)
{
    // If this binary linked at all, every header above is C++-safe. The count
    // is printed only so the check has visible output when it passes.
    std::printf("link check OK: %zu C entry points callable from C++\n",
                sizeof kRefs / sizeof kRefs[0]);
    return 0;
}
