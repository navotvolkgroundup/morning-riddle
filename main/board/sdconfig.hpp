// The kids and the timetable, from the card into NVS.
//
// Both belong on a card rather than in the build or on a network: they are the
// most personal data this device holds, they change rarely, and they should
// never leave the house. riddles.json is published to a public release; these
// two never are.
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

#endif // BOARD_SDCONFIG_HPP
