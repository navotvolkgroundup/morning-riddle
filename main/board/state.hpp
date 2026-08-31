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
#include "kids.h"
#include "riddle_decide.h"
#include "schedule.h"
}

// Initialises NVS, erasing and recreating it if it cannot be mounted.
//
// Call this ONCE, early, before anything else touches NVS -- including WiFi,
// which fails with ESP_ERR_NVS_NOT_INITIALIZED and then abort()s inside
// esp_wifi_init(). It used to happen lazily on the first state load, which is
// after the network is brought up, so the portal boot-looped the board.
bool state_nvs_init();

// Loads the stored state, or zeroes it (RS_IDLE, day 0) when nothing is
// stored. Returns false only if NVS itself is unavailable -- an absent key is
// the normal first-boot case, not an error.
bool state_load(riddle_nvs_t *st);

// Writes the state. Returns false if it did not commit, which callers should
// treat as serious: an unsaved guess is a child pressing a button and the
// board forgetting by 13:00.
bool state_save(const riddle_nvs_t *st);

// A fingerprint of the kids and the timetable, and what was last DRAWN.
//
// The page is skipped when the state machine says the panel is already correct
// (ACT_NONE), which is right for the riddle and wrong for config: editing a
// child's name in the setup page changed nothing visible until the next
// scheduled wake, because the riddle had not changed. That reads as a setup
// page that does not work. Comparing the current fingerprint against the one
// stored at the last successful draw buys exactly one redraw per edit.
uint32_t state_config_fingerprint(const kids_t *k, const kids_schedule_t *s);
uint32_t state_drawn_config();
void state_set_drawn_config(uint32_t fp);

#endif // BOARD_STATE_HPP
