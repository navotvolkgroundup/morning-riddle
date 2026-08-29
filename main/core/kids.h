// Morning Riddle: who the kids are.
//
// THIS FILE AND kids.c MUST NOT INCLUDE ANY ESP-IDF HEADER, for the same
// reason riddle_decide.c and wake_log.c must not. NVS and drawing live in
// page_riddle.cc.
//
// WHY THIS EXISTS AS A SEPARATE THING. Two accepted delight items -- the
// birthday takeover and the random name callout -- need the kids' names and
// birthdays, and that question went unanswered across three skills. Rather
// than block the build or drop the items, the plan (CEO review 16A) is hooks:
// ship the renderers, keep the data in a blob that is legal to be empty, and
// have both features simply never fire until it is filled in. Filling it in
// later is then a config edit, not a reflash, which matters a lot on a board
// that is hard to reach.
//
// WHERE THE DATA LIVES. Device NVS, imported from /sdcard/kids.json. It is
// deliberately NOT in riddles.json: that file is published to a public GitHub
// release, and children's names and birthdays have no business on a public
// URL (CEO review 8A). The card never leaves the house, and can be removed
// once the import has happened.

#ifndef KIDS_H
#define KIDS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KIDS_MAX       4
#define KID_NAME_MAX   24      // bytes, so ~7 Hebrew letters in UTF-8

typedef struct {
    char    name[KID_NAME_MAX];  // exactly as it should appear on screen
    uint8_t birth_month;         // 1-12, or 0 for "not given"
    uint8_t birth_day;           // 1-31, or 0 for "not given"
} kid_t;

typedef struct {
    uint8_t count;               // 0 is normal and means both features sleep
    uint8_t pad[3];
    kid_t   kid[KIDS_MAX];
} kids_t;

// Parses kids.json:
//
//   { "kids": [ { "name": "...", "month": 3, "day": 14 } ] }
//
// Month and day may be omitted, which simply means no birthday. Entries
// without a usable name are skipped rather than failing the file -- one
// mistyped child should not cost the others.
//
// Returns false only if the document itself is unusable, leaving *out
// untouched, so a bad card cannot wipe good cached names.
bool kids_parse(const char *json, kids_t *out);

// True if the blob is self-consistent. A blob that fails this is treated as
// absent rather than indexed into.
bool kids_valid(const kids_t *k);

// Index of a kid whose birthday falls on month/day, or -1. First match wins,
// which is the right answer for the vanishingly rare shared-birthday case.
int kids_birthday_on(const kids_t *k, int month, int day);

// Index of the kid to greet by name today, or -1 for "not today".
//
// Deterministic in `day`, which is the point rather than an implementation
// detail: the morning screen, an early reveal and the 13:00 screen are three
// separate draws of the same day, and a random pick would name a different
// kid on each. It also fires on roughly one day in KIDS_CALLOUT_ONE_IN, so
// the greeting stays a small surprise instead of becoming furniture.
#define KIDS_CALLOUT_ONE_IN 3
int kids_pick_callout(const kids_t *k, int32_t day);

#ifdef __cplusplus
}
#endif

#endif // KIDS_H
