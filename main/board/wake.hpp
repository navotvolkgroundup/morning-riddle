// Waking, sleeping, and the RTC alarm.
//
// The scheduling arithmetic is NOT here: riddle_next_wake() in core/ computes
// the next 06:30 or 13:00 local, handles DST, and is tested on the host. This
// file only talks to hardware.
//
// -------------------------------------------------------------------------
// WHY WAKE CAUSE IS CHECKED FIRST
// -------------------------------------------------------------------------
//
// Historical note, because these comments have been wrong in both directions:
// M5.begin() was measured at 52.7 s, which WAS wrong -- it costs 464 ms once
// cfg.clear_display is false. A refresh was measured at 17.1 s, which was
// RIGHT: M5Stack's own docs give 15-30 s for this panel and an independent
// project on the same board reports 15-19 s. A later ~2 s measurement here was
// the anomaly, and this file briefly rewrote the design rationale around it.
//
// So a BUTTON wake does not touch the panel for two independent reasons, and
// both hold: the panel is far too slow to acknowledge a press, AND the answer
// is withheld until the 13:00 reveal, so a redraw would repaint the whole page
// to show the same question. The press records the guess, fires the LED and the
// chirp, and goes back to sleep. The screen catches up at the next scheduled
// wake.
//
// wake_why() must still be called BEFORE M5.begin() -- the short path decides
// whether to bring the display up at all -- and it depends on nothing that
// M5.begin() sets up.

#ifndef BOARD_WAKE_HPP
#define BOARD_WAKE_HPP

#include <time.h>
#include <stdint.h>

// Pin assignments, from the M5Stack PaperColor documentation and confirmed
// against M5Unified's own board table.
#define WAKE_PIN_RTC_INT   7        // RX8130CE IRQ, active low

// THE BUTTONS ARE NAMED BY POSITION ON THE SCREEN, NOT BY M5STACK'S LABELS.
//
// M5Stack's documentation calls G10 "Button A", G9 "Button B" and G1
// "Button C". Following that naming produced a board where pressing the TOP
// button recorded choice C -- because their A is the BOTTOM button, while the
// daily page draws choice A at the top, beside the first answer.
//
// Measured on hardware, top to bottom: G1, G9, G10. A child presses the button
// beside the option they are reading, so the top button MUST be choice 0.
// These names follow the screen, and the vendor's labels are noted beside them
// so the discrepancy is visible rather than waiting to be rediscovered.
#define WAKE_PIN_BTN_TOP     1      // choice A on the page  (M5Stack calls it C)
#define WAKE_PIN_BTN_MIDDLE  9      // choice B on the page  (M5Stack calls it B)
#define WAKE_PIN_BTN_BOTTOM 10      // choice C on the page  (M5Stack calls it A)

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

// Arms the RTC for the next 06:30 or 13:00 local, and READS IT BACK.
//
// Returns false if the alarm did not verify. A board that thinks it is armed
// and is not simply never wakes again, and looks identical to a working one --
// the Waveshare build learned this the expensive way, so the readback is not
// optional here either.
bool wake_arm_next(time_t now, int *is_morning);

// Clears a pending RTC interrupt. Without this the line stays asserted and the
// board wakes again immediately, forever.
void wake_clear_alarm();

// True when USB is supplying power. Requires M5.begin() (reads VBUS via the
// PMIC), so it is only callable on the page path.
bool wake_usb_present();

// True if any of the three buttons is held right now. Safe before M5.begin().
bool wake_button_held();

// Sleeps only when it is safe to, and returns false when it refuses.
//
// A CABLED BOARD MUST STAY REACHABLE. Deep sleep powers down USB-JTAG, so a
// board that sleeps on USB vanishes from the bus and can only be recovered by
// unplugging, replugging and pressing power -- four physical actions per
// flash. The Waveshare build had exactly this rule in
// sched_power_off_if_safe(); the port dropped it and immediately paid for it.
//
// Holding a button at boot also refuses, as a deliberate escape hatch for a
// battery-powered board that would otherwise be asleep whenever you reach it.
bool wake_sleep_if_safe();

// Deep sleep until the RTC alarm or a button. Does not return. Prefer
// wake_sleep_if_safe() unless you genuinely mean to sleep regardless.
[[noreturn]] void wake_sleep();

#endif // BOARD_WAKE_HPP
