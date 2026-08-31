// Instant acknowledgement for a guess: the RGB LED and a chirp.
//
// WHY THIS EXISTS AT ALL, for two reasons that each suffice.
//
// The panel cannot answer a press. A Spectra 6 full refresh is 15-30 s per
// M5Stack's own docs, with no partial update, so a child would press and wait
// through most of a minute. (A ~2 s measurement taken in this tree once
// suggested otherwise; it was an artefact, see the README.)
//
// And it has nothing to say even if it were fast. The answer is deliberately
// held back until the 13:00 wake, so a redraw could only repeat the question
// the child is already looking at.
//
// The guess is therefore acknowledged by something that is NOT the screen.
// Without this file the interaction has no reply at all and the page is a
// poster.
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

// The board heard the press and it changes nothing: today is already answered,
// or the reveal is up. NOT a refusal -- the rotation names one child a morning,
// so three others press a board that would otherwise buzz at them daily, and a
// wall that says no to most of the house teaches them not to touch it.
//
// Deliberately short and soft. Three rhythms carry the three outcomes, so they
// are distinguishable to a child who is not looking at the LED: one long note
// for a guess that counted, one brief note for this, a descending pair for a
// genuine refusal.
void feedback_ack();

// Something was refused: a press against a stale screen, which means the board
// missed a wake. Deliberately different in both channels, and now genuinely
// rare -- it is a fault signal, not the everyday answer to a second child.
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
