// Parsing a batch of riddles.
//
// Pure: cJSON only, no IDF. The Waveshare version lived inside page_riddle.cc
// with ESP_LOG calls threaded through it, so none of its rules -- the type
// filter, the unwinnable-choices guard, the skip-and-continue behaviour --
// could be checked without a board. All of them are decisions worth testing.
//
// The document, as riddle_gen.py produces it:
//
//   { "riddles": [ { "type": "riddle",              // optional
//                    "q": "...", "a": "...",
//                    "choices": ["...","...","..."],// optional
//                    "by": "...",                   // optional
//                    "weekend": false } ] }         // optional

#ifndef RIDDLE_BATCH_H
#define RIDDLE_BATCH_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RB_MAX      40
#define RB_Q_MAX   200
#define RB_A_MAX    64
#define RB_BY_MAX   16

typedef struct {
    char q[RB_Q_MAX];
    char a[RB_A_MAX];
    char choices[3][RB_A_MAX];
    char by[RB_BY_MAX];
    bool has_choices;
    bool weekend;
} riddle_item_t;

typedef struct {
    riddle_item_t item[RB_MAX];
    int           count;
    int           skipped;      // malformed or unknown-type entries
} riddle_batch_t;

// Parses a batch document. Returns the number of usable riddles, or -1 if the
// document itself is unusable.
//
// Individual bad entries are SKIPPED rather than failing the batch: one
// mistyped riddle in thirty should cost that riddle, not the month.
int riddle_batch_parse(const char *json, riddle_batch_t *out);

#ifdef __cplusplus
}
#endif

#endif // RIDDLE_BATCH_H
