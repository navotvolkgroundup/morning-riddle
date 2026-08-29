// Instant acknowledgement for a guess: the RGB LED and a chirp.
//
// WHY THIS EXISTS AT ALL. The screen has nothing it is allowed to say. The
// answer is deliberately held back until the 13:00 wake, so redrawing on a
// press could only repeat the question the child is already looking at -- and
// a Spectra 6 has no partial update, so it would repaint the whole page (~2 s)
// to show no new information. The guess is therefore acknowledged by something
// that is NOT the screen. Without this file the interaction has no reply at
// all and the page is a poster.
//
// Both come from M5Unified. The LED is a WS2812 on G21 driven over RMT, and
// Power.begin() enables its LDO through the PM1; the speaker goes through the
// ES8311 codec over I2S with the amp enable on G46. Neither needed a
// hand-rolled driver, which was only true once cfg.clear_display=false brought
// M5.begin() down from 52.7s to 464ms and made it affordable on a button wake.
//
// Requires M5.begin().

#ifndef BOARD_FEEDBACK_HPP
#define BOARD_FEEDBACK_HPP

#include <stdint.h>

// A guess was registered. Distinct colour per choice so the acknowledgement
// says WHICH button landed, not merely that something did -- a child pressing
// B and seeing the same blink as A learns nothing about whether the board
// heard the right one.
void feedback_guess(int choice);

// Something was refused: a press outside the guessing window, or a second
// guess on a day already answered. Deliberately different in both channels.
void feedback_reject();

// Blocks until the LED and the tone have both finished, then turns the LED
// off. Call before sleeping: deep sleep cuts the RMT and I2S mid-output, so
// without this the chirp is a click and the LED may latch on and stay lit
// until the next wake -- on a battery device, for hours.
void feedback_settle();

// Returns G45 and G46 to inputs. Both are strapping pins that M5.begin()
// drives high for the audio codec, so a board that leaves them driven boots to
// DOWNLOAD mode on its next reset. Call this on every path that will not chirp
// -- feedback_settle() already ends with it.
void feedback_release_straps();

#endif // BOARD_FEEDBACK_HPP
