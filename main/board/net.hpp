// WiFi and the clock.
//
// CREDENTIALS COME FROM THE SD CARD, never from the build. This repository is
// public, and a Kconfig option or a #define would put a home network password
// into git history where it cannot be taken back out. /sdcard/wifi.json:
//
//   { "ssid": "...", "pass": "..." }
//
// The same mechanism as kids.json and schedule.json, read with the same
// sd_json reader, and the card can be removed afterwards.
//
// WHY NTP MATTERS HERE. riddle_local_day() keys the entire state machine, and
// riddle_next_wake() decides when the board wakes. Both are worthless against a
// wrong clock: the board currently believes it is day 20464 because the RTC has
// never been set, so the "16:00" alarm is 16:00 of the wrong day. Everything
// downstream is self-consistent and wrong.

#ifndef BOARD_NET_HPP
#define BOARD_NET_HPP

#include <stdbool.h>

// Brings up WiFi using credentials from the card. Returns false when the card,
// the file or the network is missing -- all of which are survivable: the page
// still draws from cache.
bool net_connect(int timeout_ms = 20000);

// Asks an NTP server for the time and writes it to BOTH the system clock and
// the RTC. Returns false on timeout.
//
// Writing the RTC is the point: the system clock dies with the power, and this
// board deep-sleeps between wakes. An NTP sync that only set the system clock
// would be forgotten before it was ever used.
bool net_sync_time(int timeout_ms = 15000);

// Shuts WiFi down. Deep sleep does this anyway, but an explicit stop keeps the
// radio off during the seventeen seconds the panel spends refreshing.
void net_stop();

#endif // BOARD_NET_HPP
