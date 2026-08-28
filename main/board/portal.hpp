// WiFi setup over the board's own access point.
//
// The board publishes an open network, you join it from a phone, type your
// home network's name and password into a page it serves, and it stores them
// in NVS. Exactly how the M5Stack demo was set up, and how anyone other than
// the author would expect to configure a device on a wall.
//
// WHY THIS EXISTS rather than only reading the SD card: changing WiFi should
// not require pulling the card out of a device that is screwed to a wall. The
// card route stays as a fallback and for the things that genuinely belong on a
// card -- the kids' names and the school timetable, which are not secrets to
// be typed but data to be edited.
//
// THE PASSWORD GOES PHONE -> NVS AND NOWHERE ELSE. It is not logged at any
// level, not echoed back into the form, and not written to the SD card. The
// only thing that ever appears in a log is the network's name.

#ifndef BOARD_PORTAL_HPP
#define BOARD_PORTAL_HPP

#include <stddef.h>

// The network the board publishes while waiting to be configured.
#define PORTAL_AP_SSID "Morning-Riddle-Setup"

// True if credentials are stored in NVS.
bool portal_have_credentials();

// Reads stored credentials. Returns false when none are stored.
bool portal_load_credentials(char *ssid, size_t ssid_len,
                             char *pass, size_t pass_len);

// Brings up the access point and serves the setup page until someone submits
// credentials or `timeout_ms` elapses. Returns true if something was stored.
//
// Blocking on purpose. There is nothing else for the board to do: without a
// network it has no riddles, and its clock is wrong.
bool portal_run(int timeout_ms = 300000);

#endif // BOARD_PORTAL_HPP
