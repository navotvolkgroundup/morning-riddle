// Morning Riddle: the wake ring.
//
// THIS FILE AND wake_log.c MUST NOT INCLUDE ANY ESP-IDF HEADER, for the same
// reason riddle_decide.c must not: the ring arithmetic is where an off-by-one
// hides, and an off-by-one here is invisible on the device -- you would see a
// slightly odd ordering and shrug. NVS, the battery gauge and the drawing all
// live in page_riddle.cc.
//
// WHY A LOG AT ALL. This board is powered off about 99.98% of the time and
// ESP_LOG dies with the rail, so three of the eight failure modes in the CEO
// review -- alarm not armed, batch truncated, NVS commit lost -- all present
// identically: a wall still showing yesterday's riddle, which looks exactly
// like a working device. You would notice on day three, by finding the joke
// stale. This ring is what turns those into something readable in ten seconds
// from the diagnostics screen. (CEO review 13A.)
//
// It also carries the number the whole design is betting on. The stated
// success test is "a kid mentions the riddle unprompted"; the measurable proxy
// is how many days somebody actually pressed a button, and that is free here.

#ifndef WAKE_LOG_H
#define WAKE_LOG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WAKE_LOG_N 14           // ~1 week of two-wake days

// What happened on a wake. Anything other than WO_OK is worth a look.
typedef enum {
    WO_OK = 0,
    WO_NO_BATCH,            // nothing to show; the wall said so
    WO_FETCH_FAILED,        // network unreachable; kept the old queue
    WO_FETCH_PARTIAL,       // truncated or short body; kept the old queue
    WO_PARSE_FAILED,        // batch arrived and did not parse
    WO_SD_IMPORT,           // a batch came off the SD card
    WO_ALARM_UNVERIFIED,    // the next alarm did NOT read back. See 3A.
    WO_NVS_FAILED,          // state did not commit; the day may repeat
} wake_outcome_e;

// Bit flags, so one wake can be several things at once.
#define WF_GUESSED   0x01       // someone pressed a choice
#define WF_CORRECT   0x02       // ...and it was right
#define WF_FETCHED   0x04       // a new batch was stored this wake
#define WF_USB       0x08       // USB was attached, so no power-off

typedef struct {
    uint32_t when;      // unix seconds, UTC. 0 = clock was not readable.
    uint8_t  reason;    // wake_reason_e from riddle_decide.h
    uint8_t  outcome;   // wake_outcome_e
    int8_t   battery;   // percent, or -1 if the gauge did not answer
    uint8_t  flags;     // WF_*
    uint16_t stack_free;// smallest free stack seen, in bytes. 0 = unknown.
    uint16_t idx;       // which riddle was on screen
} wake_rec_t;

// The whole ring, as it sits in NVS. Fixed layout on purpose: a size change
// invalidates the blob, and the loader treats a wrong size as "start fresh"
// rather than reinterpreting old bytes as a new struct.
typedef struct {
    uint8_t    head;                // next slot to write
    uint8_t    count;               // entries in use, saturating at WAKE_LOG_N
    uint16_t   pad;                 // keep the records 4-byte aligned
    wake_rec_t rec[WAKE_LOG_N];
} wake_ring_t;

// Appends one record, overwriting the oldest once full.
void wake_ring_push(wake_ring_t *ring, const wake_rec_t *r);

// Copies up to `max` records into `out`, NEWEST FIRST. Returns how many.
int wake_ring_read(const wake_ring_t *ring, wake_rec_t *out, int max);

// True if the ring is self-consistent. A blob that fails this is treated as
// corrupt and reset, rather than indexed off the end of the array.
bool wake_ring_valid(const wake_ring_t *ring);

// Consecutive most-recent days on which someone guessed, ignoring wakes that
// were not riddle days. Cheap participation read-out for the diagnostics
// screen; the authoritative streak lives in riddle_nvs_t.
int wake_ring_recent_guesses(const wake_ring_t *ring);

// One-word label for an outcome, for the diagnostics screen.
const char *wake_outcome_name(uint8_t outcome);

#ifdef __cplusplus
}
#endif

#endif // WAKE_LOG_H
