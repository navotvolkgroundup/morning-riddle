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

// Whose turn it is today. -1 only when there are no kids.
//
// A ROTATION, NOT A LOTTERY. This used to fire on roughly one day in three and
// pick a kid by hash, so a guess belonged to nobody and a child could go a week
// without the wall ever saying their name. Every morning now names exactly one
// of them, in strict rotation, and their streak is theirs.
//
// Deterministic in `day`, which is the point rather than an implementation
// detail: the morning screen, an early reveal and the 13:00 screen are three
// separate draws of the same day, and a random pick would name a different kid
// on each.
//
// `day % count` is the whole rule, and the old comment rejected exactly this
// on the grounds that it names the kids in strict rotation. That was the right
// call when the greeting was meant to be a surprise and the wrong one now that
// it is meant to be a turn. With four kids and a seven-day week the two cycles
// are coprime, so nobody is permanently stuck with Mondays.
int kids_turn_today(const kids_t *k, int32_t day);

// How many days until this kid's turn comes round again. Equals the number of
// kids, and exists so the streak rule can ask "was their last turn the previous
// one?" without the caller re-deriving it.
int kids_turn_period(const kids_t *k);

#ifdef __cplusplus
}
#endif

#endif // KIDS_H
