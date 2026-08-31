// Fetching today's picture. See core/strip.h for the format and for why the
// generator makes it rather than the board.

#ifndef BOARD_STRIP_FETCH_HPP
#define BOARD_STRIP_FETCH_HPP

#include <stddef.h>
#include <stdint.h>

extern "C" {
#include "strip.h"
}

// Fetches strip-<idx>.bin from the same release the batch comes from and
// validates it. Returns true and fills `out` (pointing into `buf`) on success.
//
// A 404 IS THE NORMAL CASE, not an error. Most items have no picture, and a
// day without one draws the page it drew before pictures existed. Nothing here
// logs at error level for a missing strip.
//
// NOT CACHED, deliberately. The NVS partition is 24KB and the batch already
// fills most of it; an 18KB image would not fit, and a picture is the one zone
// on this page that is genuinely nice to have. No network in the morning means
// no picture that day, which degrades exactly like the weather does.
//
// Must be called while the radio is up. `buf` needs STRIP_MAX_BYTES.
#define STRIP_MAX_BYTES (STRIP_HDR_BYTES + (STRIP_W / 2) * STRIP_H_MAX)

bool strip_fetch(int idx, uint8_t *buf, size_t cap, strip_t *out);

#endif // BOARD_STRIP_FETCH_HPP
