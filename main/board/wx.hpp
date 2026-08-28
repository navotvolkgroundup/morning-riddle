// Weather: fetch and cache.
//
// Parsing and staleness live in core/weather.c and are host-tested. This is
// the fetch and the NVS cache, mirroring board/batch.cpp.
//
// CACHED, AND STALE IS SHOWN RATHER THAN HIDDEN. A failed fetch must never
// cost the page: the board still draws yesterday's reading with an "old"
// marker, because a slightly wrong temperature that admits it is better than a
// blank box, and far better than a confident wrong one.
//
// The fetch is MORNING ONLY. The afternoon wake redraws from cache -- that
// reading is about nine hours old, which is what WEATHER_STALE_SECS is
// calibrated against, and a second fetch would double the network cost and the
// failure surface for no benefit.

#ifndef BOARD_WX_HPP
#define BOARD_WX_HPP

extern "C" {
#include "weather.h"
}

// Coordinates are DELIBERATELY COARSE -- city level, one decimal place. This
// repository is public, and a home's exact latitude and longitude is not a
// thing to publish. Weather does not vary meaningfully across the difference.
#define WX_LAT "32.08"
#define WX_LON "34.78"

// Loads the cached reading. Returns false when nothing is stored.
bool wx_load(weather_t *out);

// Fetches and caches. A failure leaves the cache intact and returns false --
// the caller should carry on and draw what it has.
bool wx_fetch(weather_t *out, uint32_t now_utc);

#endif // BOARD_WX_HPP
