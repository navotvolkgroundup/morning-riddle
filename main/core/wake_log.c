// Morning Riddle: the wake ring. See wake_log.h for the no-IDF rule.

#include "wake_log.h"

#include <string.h>

void wake_ring_push(wake_ring_t *ring, const wake_rec_t *r)
{
    if (!ring || !r) return;
    if (!wake_ring_valid(ring)) {
        // A blob that does not make sense is worth less than an empty one.
        memset(ring, 0, sizeof *ring);
    }
    ring->rec[ring->head] = *r;
    ring->head = (uint8_t)((ring->head + 1) % WAKE_LOG_N);
    if (ring->count < WAKE_LOG_N) ring->count++;
}

int wake_ring_read(const wake_ring_t *ring, wake_rec_t *out, int max)
{
    if (!ring || !out || max <= 0 || !wake_ring_valid(ring)) return 0;

    int n = ring->count < max ? ring->count : max;
    for (int i = 0; i < n; i++) {
        // head points at the NEXT slot to write, so head-1 is the newest.
        // Adding WAKE_LOG_N before the modulo keeps the index non-negative
        // without relying on how C treats a negative operand.
        int slot = (ring->head - 1 - i + 2 * WAKE_LOG_N) % WAKE_LOG_N;
        out[i] = ring->rec[slot];
    }
    return n;
}

bool wake_ring_valid(const wake_ring_t *ring)
{
    if (!ring) return false;
    if (ring->head >= WAKE_LOG_N) return false;
    if (ring->count > WAKE_LOG_N) return false;
    return true;
}

int wake_ring_recent_guesses(const wake_ring_t *ring)
{
    wake_rec_t recs[WAKE_LOG_N];
    int n = wake_ring_read(ring, recs, WAKE_LOG_N);
    int days = 0;
    uint32_t last_day = 0;

    for (int i = 0; i < n; i++) {
        if (!recs[i].when) continue;                 // clock was unreadable
        uint32_t day = recs[i].when / 86400u;
        if (last_day && day == last_day) continue;   // same day, already counted
        if (!(recs[i].flags & WF_GUESSED)) break;    // the run ends here
        // A gap of more than one day also ends the run.
        if (last_day && last_day - day > 1) break;
        days++;
        last_day = day;
    }
    return days;
}

const char *wake_outcome_name(uint8_t outcome)
{
    switch (outcome) {
    case WO_OK:               return "ok";
    case WO_NO_BATCH:         return "no batch";
    case WO_FETCH_FAILED:     return "fetch failed";
    case WO_FETCH_PARTIAL:    return "partial fetch";
    case WO_PARSE_FAILED:     return "bad batch";
    case WO_SD_IMPORT:        return "SD import";
    case WO_ALARM_UNVERIFIED: return "ALARM UNSET";   // the one that matters
    case WO_NVS_FAILED:       return "NVS failed";
    default:                  return "?";
    }
}
