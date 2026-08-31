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
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RB_MAX      40
#define RB_Q_MAX   200
#define RB_A_MAX    64
#define RB_BY_MAX   16
#define RB_WHY_MAX 160

// What kind of thing this is. The renderer is the same for all of them -- a
// question, up to three choices, an answer -- and only the label above the
// lead changes.
//
// ROTATING THE FORM IS WHAT KEEPS A DAILY RITUAL FROM BECOMING WALLPAPER.
// Thirty riddles is six weeks; after that the shape of the page is the same
// every morning forever, and a wall display that has stopped being looked at
// has failed no matter how well it refreshes. A joke or a word of the day
// costs nothing here because it fits the shape that already exists.
typedef enum {
    RK_RIDDLE = 0,      // "what has teeth and never bites?"
    RK_JOKE,            // punchline in `a`, no choices
    RK_WORD,            // word of the day; `a` is the meaning
    RK_MATH,            // mental arithmetic
    RK_KIND_COUNT
} riddle_kind_e;

typedef struct {
    char q[RB_Q_MAX];
    char a[RB_A_MAX];
    char choices[3][RB_A_MAX];
    char by[RB_BY_MAX];
    // WHY THE ANSWER IS THE ANSWER, and the reason this field earns 160 bytes
    // times forty. The reveal used to print one word in red and stop, which is
    // the moment a riddle either teaches something or is just a quiz a child
    // got wrong. Optional: an empty `why` draws the page exactly as before.
    char why[RB_WHY_MAX];
    uint8_t kind;               // riddle_kind_e
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
