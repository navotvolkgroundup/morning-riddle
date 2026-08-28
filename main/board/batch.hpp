// Getting the riddles onto the board and keeping them there.
//
// Parsing lives in core/riddle_batch.c and is host-tested. This is the half
// that fetches over HTTPS and persists, which is the half that cannot be.
//
// THE BATCH IS CACHED IN NVS. The board wakes twice a day and sleeps in
// between, so re-fetching every wake would mean the page depends on the
// network being up at 06:30 -- and a morning with no riddle is the failure
// this whole design exists to avoid. Fetch when the cache is empty or stale;
// otherwise draw what is already there.

#ifndef BOARD_BATCH_HPP
#define BOARD_BATCH_HPP

extern "C" {
#include "riddle_batch.h"
}

// The published batch. A GitHub release asset, so it can be updated without
// touching the device -- the same URL the Waveshare build used.
#define BATCH_URL                                                       \
    "https://github.com/navotvolkgroundup/ESP32-S3-ePaper-3.97"         \
    "/releases/download/riddles-latest/riddles.json"

// Loads the cached batch from NVS. Returns the number of riddles, 0 if none.
int batch_load(riddle_batch_t *out);

// Fetches BATCH_URL, parses it, and caches it only if it parses.
//
// A batch that arrives truncated or malformed must NOT replace a good cached
// one: a bad download would otherwise cost every riddle the board has. Returns
// the number of riddles fetched, or -1 on any failure.
int batch_fetch(riddle_batch_t *out);

#endif // BOARD_BATCH_HPP
