// Morning Riddle: the decision core.
//
// THIS FILE AND riddle_decide.c MUST NOT INCLUDE ANY ESP-IDF HEADER.
// No esp_err_t, no ESP_LOGx, no nvs_handle_t, no freertos. Only C99 plus
// <time.h>, which the host and the target both have. That constraint is the
// entire point: it is what lets 22 of this feature's branches run on a Mac in
// milliseconds instead of on a board that is reachable a few minutes an hour.
// The moment an IDF include lands here, `make test` stops compiling and the
// DST bug below becomes unobservable until late October. (Eng review D9.)
//
// I/O stays in the caller. This file reads a struct and returns an enum.

#ifndef RIDDLE_DECIDE_H
#define RIDDLE_DECIDE_H

#include <stdint.h>
#include <time.h>

// Local wall-clock times of the two daily events.
#define RIDDLE_MORNING_HOUR  6
#define RIDDLE_MORNING_MIN  30
#define RIDDLE_REVEAL_HOUR  13
#define RIDDLE_REVEAL_MIN    0

// POSIX TZ for Israel, including the DST rules.
//
// This is why the schedule is stored in UTC and local time is DERIVED. The
// vendor's timezone table (page_clock.cc:43) is 25 fixed offsets with no DST,
// so a schedule pinned to "UTC+3" silently becomes 07:30 when IDT ends on
// 2026-10-25 -- after the kids have left for school, with no symptom. The C
// library already owns these rules; do not hand-roll them.
//   IST-2      standard time, UTC+2
//   IDT        daylight name
//   M3.4.4/26  starts March, 4th week, Thursday, 02:00 the following day
//   M10.5.0    ends October, last Sunday, 02:00
#define RIDDLE_TZ "IST-2IDT,M3.4.4/26,M10.5.0"

#define RIDDLE_NO_GUESS (-1)

typedef enum {
    RS_IDLE = 0,        // nothing shown yet (first boot)
    RS_QUESTION_SHOWN,  // riddle is on the panel, no guess made
    RS_GUESSED,         // a guess was made; answer not yet revealed
    RS_ANSWER_SHOWN,    // answer is on the panel; terminal until tomorrow
} riddle_state_e;

typedef enum {
    WAKE_MORNING = 0,   // the 06:30 alarm
    WAKE_AFTERNOON,     // the 13:00 alarm
    WAKE_GUESS,         // Up/Function/Down pressed with a choice
    WAKE_REVEAL,        // reveal-early pressed
    WAKE_MENU,          // entered from the menu tile; must not mutate anything
} wake_reason_e;

typedef enum {
    ACT_NONE = 0,       // nothing to draw; what is on the panel is correct
    ACT_SHOW_QUESTION,
    ACT_SHOW_ANSWER,
    ACT_SHOW_RESULT,    // right/wrong feedback immediately after a guess
} riddle_action_e;

// Everything that must survive a full power-off. There is no deep sleep on
// this board -- axp_pwr_off() cuts the rails -- so RTC_DATA_ATTR is useless
// and this struct is the only memory the feature has. It lives in NVS.
typedef struct {
    int32_t  day;              // local civil day of the riddle now displayed
    int32_t  last_played_day;  // local civil day of the last guess, for streaks
    uint16_t idx;              // index into the batch
    uint16_t streak;           // consecutive days with a guess (participation)
    uint8_t  state;            // riddle_state_e
    int8_t   guess;            // 0..2, or RIDDLE_NO_GUESS
} riddle_nvs_t;

typedef struct {
    uint8_t  reason;    // wake_reason_e
    int8_t   guess;     // choice index for WAKE_GUESS, else RIDDLE_NO_GUESS
    uint16_t batch_n;   // riddles available; 0 means the batch is empty
    int32_t  today;     // local civil day, from riddle_local_day()
} riddle_input_t;

#ifdef __cplusplus
extern "C" {
#endif

// --- schedule ---------------------------------------------------------------

// Local civil day number (days since 1970-01-01 local). The day key for
// "have we already shown today's riddle?".
int32_t riddle_local_day(time_t utc, const char *tz);

// UTC instant of the next 06:30 or 13:00 local, strictly after `utc`.
// `is_morning` (may be NULL) receives 1 for the 06:30 slot, 0 for 13:00.
time_t riddle_next_wake(time_t utc, const char *tz, int *is_morning);

// --- state machine ----------------------------------------------------------

// Reads `in`, updates `st` in place, returns what to draw.
// Pure apart from mutating `st`: no clock read, no I/O, no allocation.
riddle_action_e riddle_decide(const riddle_input_t *in, riddle_nvs_t *st);

#ifdef __cplusplus
}
#endif

#endif // RIDDLE_DECIDE_H
