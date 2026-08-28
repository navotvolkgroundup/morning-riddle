// Waking, sleeping, and the RTC alarm.
//
// The scheduling arithmetic is NOT here: riddle_next_wake() in core/ computes
// the next 06:30 or 16:00 local, handles DST, and is tested on the host. This
// file only talks to hardware.
//
// -------------------------------------------------------------------------
// THE 52-SECOND PROBLEM, AND WHY WAKE CAUSE IS CHECKED FIRST
// -------------------------------------------------------------------------
//
// M5.begin() takes 52.7 seconds on this board -- Spectra 6 panel
// initialisation, measured, not estimated. A full refresh is another 17.1.
// Deep sleep does not avoid it: waking restarts the application, so every
// wake that initialises the display pays ~70 seconds.
//
// That is fine twice a day at 06:30 and 16:00, when nobody is watching the
// page appear. It is unusable for a button press: a child pressing a guess
// cannot wait 70 seconds for acknowledgement.
//
// So a BUTTON wake must never call M5.begin(). It records the guess, fires the
// LED and the chirp, and goes straight back to sleep -- milliseconds, no panel
// touched. The screen catches up at the next scheduled wake, which is exactly
// what the design decided when it moved the reveal to 16:00.
//
// This is why wake_why() must be called BEFORE M5.begin(), and why it depends
// on nothing that M5.begin() sets up.

#ifndef BOARD_WAKE_HPP
#define BOARD_WAKE_HPP

#include <time.h>
#include <stdint.h>

// Pin assignments, from the M5Stack PaperColor documentation and confirmed
// against M5Unified's own board table.
#define WAKE_PIN_RTC_INT   7        // RX8130CE IRQ, active low
#define WAKE_PIN_BTN_A    10
#define WAKE_PIN_BTN_B     9
#define WAKE_PIN_BTN_C     1

enum class wake_cause {
    cold,       // power-on or reset: not a wake at all
    alarm,      // the RTC fired -- draw the page
    button,     // somebody pressed a guess -- do NOT touch the display
    other,      // some other deep-sleep source
};

// Why this boot happened. Safe to call before M5.begin(), and deliberately
// depends on nothing it initialises.
wake_cause wake_why();

// Which button woke us, or -1. Only meaningful for wake_cause::button.
int wake_button_index();

// Copies the RTC into the system clock. Requires M5.begin().
bool wake_sync_clock();

// Arms the RTC for the next 06:30 or 16:00 local, and READS IT BACK.
//
// Returns false if the alarm did not verify. A board that thinks it is armed
// and is not simply never wakes again, and looks identical to a working one --
// the Waveshare build learned this the expensive way, so the readback is not
// optional here either.
bool wake_arm_next(time_t now, int *is_morning);

// Clears a pending RTC interrupt. Without this the line stays asserted and the
// board wakes again immediately, forever.
void wake_clear_alarm();

// Deep sleep until the RTC alarm or a button. Does not return.
[[noreturn]] void wake_sleep();

#endif // BOARD_WAKE_HPP
