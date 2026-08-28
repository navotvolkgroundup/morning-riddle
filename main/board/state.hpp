// Persisting riddle_nvs_t.
//
// riddle_decide() is deliberately free of any IDF header, so it cannot store
// anything itself: it reads a struct and returns an enum. This is the other
// half -- the part that knows about NVS and nothing about the rules.
//
// The struct is written whole rather than field by field. It is 16 bytes and
// the fields are only meaningful together: a streak saved without the day it
// belongs to, or a guess without the state that makes it valid, is worse than
// no save at all.

#ifndef BOARD_STATE_HPP
#define BOARD_STATE_HPP

extern "C" {
#include "riddle_decide.h"
}

// Loads the stored state, or zeroes it (RS_IDLE, day 0) when nothing is
// stored. Returns false only if NVS itself is unavailable -- an absent key is
// the normal first-boot case, not an error.
bool state_load(riddle_nvs_t *st);

// Writes the state. Returns false if it did not commit, which callers should
// treat as serious: an unsaved guess is a child pressing a button and the
// board forgetting by 16:00.
bool state_save(const riddle_nvs_t *st);

#endif // BOARD_STATE_HPP
