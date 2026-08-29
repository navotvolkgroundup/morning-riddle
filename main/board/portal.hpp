// WiFi setup over the board's own access point.
//
// The board publishes an open network, you join it from a phone, type your
// home network's name and password into a page it serves, and it stores them
// in NVS. Exactly how the M5Stack demo was set up, and how anyone other than
// the author would expect to configure a device on a wall.
//
// WHY THIS EXISTS rather than only reading the SD card: changing WiFi should
// not require pulling the card out of a device that is screwed to a wall.
//
// THE KIDS AND THE TIMETABLE ARE HERE TOO, and that was not the original plan.
// They were meant to live on the card, edited on a laptop as JSON. This
// board's card reader never completed one data-block transfer -- SPI and SDMMC
// both fail at sdmmc_init_sd_ssr, before any filesystem is touched, so no
// format could fix it -- which left the two most important pieces of content
// with no route onto the device at all. The card path still works and still
// wins when a card is readable; this is the route that does not depend on it.
//
// FIELDS, NOT JSON. A parent typing a timetable into a phone should not be
// typing braces. Each day is one text box of comma-separated subjects, which
// is EXACTLY what schedule_t stores after parsing -- so this path needs no
// parser and no serialiser, and the form is prefilled straight from the
// structs the board is already holding.
//
// THE PASSWORD GOES PHONE -> NVS AND NOWHERE ELSE. It is not logged at any
// level, not echoed back into the form, and not written to the SD card. The
// only thing that ever appears in a log is the network's name.

#ifndef BOARD_PORTAL_HPP
#define BOARD_PORTAL_HPP

#include <stddef.h>

extern "C" {
#include "kids.h"
#include "schedule.h"
}

// The network the board publishes while waiting to be configured.
#define PORTAL_AP_SSID "Morning-Riddle-Setup"

// True if credentials are stored in NVS.
bool portal_have_credentials();

// Reads stored credentials. Returns false when none are stored.
bool portal_load_credentials(char *ssid, size_t ssid_len,
                             char *pass, size_t pass_len);

// Brings up the access point and serves the setup page until someone submits
// it or `timeout_ms` elapses.
//
// `kids` and `sched` are read to prefill the form and WRITTEN IN PLACE when it
// is submitted, so the caller draws the page from what was just typed rather
// than from what it loaded a moment earlier. Either may be NULL, which drops
// that section from the page.
//
// Returns true if WiFi credentials are stored when it finishes -- which
// includes credentials that were already there before, since a visit that only
// edits the timetable must not read as "still no network".
//
// Blocking on purpose. There is nothing else for the board to do while
// somebody is typing into it.
bool portal_run(kids_t *kids, schedule_t *sched, int timeout_ms = 300000);

#endif // BOARD_PORTAL_HPP
