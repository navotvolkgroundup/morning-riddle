// The kids and the timetable, from the card into NVS.
//
// Both belong on a card rather than in the build or on a network: they are the
// most personal data this device holds, they change rarely, and they should
// never leave the house. riddles.json is published to a public release; these
// two never are.
//
// The setup page writes the same two NVS keys (see sdconfig_store_*), which
// is now the primary route: this board's card reader never completed a single
// data-block transfer, so the card path is the fallback rather than the plan.
//
// SD -> NVS, so the card can come back out. A device on a wall should not
// depend on a card staying seated for years, and this board has already shown
// how easily a card stops answering. The card is the way to CHANGE the data,
// not the way to read it every morning.
//
// A missing card, or a missing file, is the ordinary case and leaves whatever
// was cached. Only a file that parses replaces what is stored.

#ifndef BOARD_SDCONFIG_HPP
#define BOARD_SDCONFIG_HPP

extern "C" {
#include "kids.h"
#include "schedule.h"
}

// Loads both from NVS, then lets a readable card override them.
void sdconfig_load(kids_t *kids, schedule_t *sched);

// The cached names and timetable and nothing else: no card, no logging, no
// import. The guess path needs the kids blob to credit a turn to somebody, and
// it runs between a child pressing a button and the LED answering -- the whole
// reason that path was moved above sdconfig_load in the first place.
void sdconfig_load_cached(kids_t *kids, schedule_t *sched);

// Stores what the setup page collected. Same NVS keys the card import writes,
// so the two routes are interchangeable and neither is privileged: whichever
// wrote last is what the board knows.
void sdconfig_store_kids(const kids_t *kids);
void sdconfig_store_schedule(const schedule_t *sched);

#endif // BOARD_SDCONFIG_HPP
