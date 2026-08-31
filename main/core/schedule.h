// Morning Riddle: today's school timetable, as one line.
//
// IDF-free, same rule as riddle_decide / wake_log / kids / weather. cJSON is
// allowed (vendored library, host-compilable), so parsing is tested too.
//
// ONE LINE, NOT A GRID. A six-period table costs about 180px and pushes the
// daily page over its 800px budget; a single wrapped line costs 60 and fits.
// It also answers the only question a timetable on a wall needs to answer,
// which is what to pack. Nothing on a page that redraws twice a day can tell
// you what is happening at 11:40 anyway.
//
// DAYS ARE NAMED, NOT INDEXED. The Israeli school week runs Sunday to
// Thursday, so a file written assuming Monday is row zero shifts every day
// silently, and the reader only finds out by turning up without a gym kit.
// Naming the keys removes the convention, so there is nothing to get wrong:
//
//   {"days": {"sun": ["מתמטיקה", "אנגלית"], "mon": [...], ...}}
//
// Days may be omitted. A missing day is an empty line and the zone simply
// does not draw, which is correct for a weekend.
//
// SEPARATOR IS ASCII, deliberately. hebrew.inc draws U+05D0-U+05EA and ASCII
// 0x20-0x7E and nothing else, so the middle dot (U+00B7) that reads nicely in
// a design document would render as an invisible gap on the panel. Comma-space
// it is. (This corrected the daily-page design doc, which used the dot.)

#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCHED_DAYS       7
#define SCHED_LINE_MAX 128      // bytes; Hebrew is 2 per letter in UTF-8
#define SCHED_SEP      ", "

typedef struct {
    // Pre-joined at parse time rather than at draw time: the draw path runs on
    // a cold-booted board with a panel waiting on it, and joining strings is
    // work that only needs doing when the file changes.
    char line[SCHED_DAYS][SCHED_LINE_MAX];
} schedule_t;

// ONE TIMETABLE PER CHILD, because they are in different years.
//
// There was a single schedule_t for the household, which is only correct when
// every child is in one class. They are not: one is in ו1 and one in ג2, and
// showing a sixth-year timetable to an eight-year-old is worse than showing
// none -- it is confidently wrong, which is the failure this project keeps
// coming back to.
//
// The page draws the timetable belonging to whoever the rotation named that
// morning, which is also what makes the turn line mean something: the page is
// addressed to one child and carries that child's day.
//
// Indexed by the same kid index kids_turn_today() returns. 3.5KB in NVS, which
// is a separate key from the names so a bad import of one cannot lose the
// other.
typedef struct {
    schedule_t kid[4];          // KIDS_MAX, not included here to keep this
                                // header free of kids.h
} kids_schedule_t;

// Weekday for a civil day number, 0 = Sunday .. 6 = Saturday.
//
// 1970-01-01 was a THURSDAY, so the offset is 4. Verified against real dates
// rather than reasoned about: civil day 0, 20454 (2026-01-01, Thursday),
// 20692 (2026-08-27, Thursday), 20694 (Saturday) and 20695 (Sunday).
int schedule_weekday(int32_t civil_day);

// The weekday in Hebrew for the page header, Sunday = 0. Static string, never
// NULL; out-of-range clamps instead of indexing off the end.
const char *schedule_weekday_he(int wd);

// Parses a schedule document. Days may be missing; unusable subject strings
// are skipped rather than failing the whole file. Returns false only if the
// document itself is unusable, and leaves *out untouched in that case so a bad
// card cannot wipe a good stored timetable.
bool schedule_parse(const char *json, schedule_t *out);

// Today's line, or "" when that day has nothing. Never returns NULL.
const char *schedule_for_day(const schedule_t *s, int32_t civil_day);

// True if every day is empty -- the state a board with no schedule.json is in,
// which is normal and means the zone does not draw.
bool schedule_is_empty(const schedule_t *s);

#ifdef __cplusplus
}
#endif

#endif // SCHEDULE_H
