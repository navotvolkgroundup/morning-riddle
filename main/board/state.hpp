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

// Initialises NVS, erasing and recreating it if it cannot be mounted.
//
// Call this ONCE, early, before anything else touches NVS -- including WiFi,
// which fails with ESP_ERR_NVS_NOT_INITIALIZED and then abort()s inside
// esp_wifi_init(). It used to happen lazily on the first state load, which is
// after the network is brought up, so the portal boot-looped the board.
bool state_nvs_init();

// Increments a boot counter in NVS and returns the new value.
//
// THE ONLY WITNESS THIS BOARD HAS. Serial forces the ROM bootloader when the
// port is opened, the panel may not be reaching the glass, and the LED has
// never been observed working. A counter in flash is testable from the host
// with esptool alone: dump the nvs partition, power-cycle, dump again. If the
// bytes changed, the application ran. Nothing else currently proves that.
uint32_t state_bump_boot_count();

// Records why this boot happened, and reports what the PREVIOUS boot recorded.
//
// A battery wake has no serial port -- USB-JTAG is gone with the cable -- so
// the one boot that matters most, the button press a child actually makes, is
// invisible. This carries its wake cause across to the next cabled boot, where
// it can be read. Without it the guess path can only be tested by inference.
void state_note_wake(int cause, int button);

// Marks that wake_sleep() was reached and deep sleep was actually entered.
// Cleared by state_note_wake() on the next boot, which reports it. Together
// they answer the only question a battery test can otherwise not answer: did
// the board sleep and fail to wake on the button, or never sleep at all?
void state_note_sleeping();

// Records why the board chose to stay awake instead of sleeping: the VBUS
// reading in millivolts and whether a button read as held. Reported on the
// next cabled boot. On battery there is no serial, so a refusal to sleep is
// otherwise completely silent -- and a board that never sleeps flattens its
// cell overnight without ever saying why.
void state_note_awake(int vbus_mv, bool button_held);

// Records the inputs and outcome of a guess decision, reported on the next
// boot. The guess path runs for about a second between a button wake and deep
// sleep, which is far too short to catch on a serial port that does not exist
// until the board wakes -- so the only way to see why a guess was refused is
// to write it down.
void state_note_guess(long today, long st_day, int st_state, int act, int btn);

// Loads the stored state, or zeroes it (RS_IDLE, day 0) when nothing is
// stored. Returns false only if NVS itself is unavailable -- an absent key is
// the normal first-boot case, not an error.
bool state_load(riddle_nvs_t *st);

// Writes the state. Returns false if it did not commit, which callers should
// treat as serious: an unsaved guess is a child pressing a button and the
// board forgetting by 13:00.
bool state_save(const riddle_nvs_t *st);

#endif // BOARD_STATE_HPP
