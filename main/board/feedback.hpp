// Instant acknowledgement for a guess: the RGB LED and a chirp.
//
// WHY THIS EXISTS AT ALL. A Spectra 6 refresh is 17.1 seconds and there is no
// partial update, so the screen cannot answer a button press -- a child would
// press and wait. The design's answer is that the guess is acknowledged by
// something that is NOT the screen, and the reveal waits for the 16:00 wake.
// This file is that acknowledgement; without it the interaction has no reply
// at all and the page is a poster.
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

#endif // BOARD_FEEDBACK_HPP
