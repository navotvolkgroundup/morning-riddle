// The microSD card.
//
// Config comes from the card rather than from the build: WiFi credentials, the
// kids' names and birthdays, the school timetable. None of that belongs in a
// public repository, and none of it should need a reflash to change.
//
// Pins from the M5Stack PaperColor documentation, cross-checked against
// M5Unified's own board table: CLK G15, MOSI G13, MISO G14, CS G47.

#ifndef BOARD_SDCARD_HPP
#define BOARD_SDCARD_HPP

#define SD_MOUNT_POINT "/sdcard"

// Mounts the card. Returns false when there is no card, or it is unreadable.
//
// A MISSING CARD IS NORMAL, not an error: the board runs without one, showing
// whatever it has cached. Callers should degrade, not fail.
bool sd_mount();

// Unmounts and releases the SPI bus. Worth doing before sleeping so the card
// is not left half-powered through deep sleep.
void sd_unmount();

#endif // BOARD_SDCARD_HPP
