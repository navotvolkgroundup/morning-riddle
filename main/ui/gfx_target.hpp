// The one surface everything draws into.
//
// WHY A CANVAS AND NOT M5.Display DIRECTLY. Drawing straight to the panel and
// calling M5.Display.display() does not drive this display. It renders, it
// reports a refresh, and no ink moves -- measured at 1909 ms every time,
// whatever changed on screen, on a panel whose own initialisation takes 52 s.
//
// M5Stack's own firmware never calls display(). It draws into an off-screen
// M5Canvas and calls pushSprite(), and that path measures 17134 ms and puts
// real ink on the glass -- a genuine Spectra 6 waveform, matching the 15-30 s
// the datasheet gives.
//
// This cost a day and a half. Everything upstream was working the whole time:
// the page was drawn correctly into a buffer that was never sent.
#ifndef UI_GFX_TARGET_HPP
#define UI_GFX_TARGET_HPP

#include <M5Unified.h>

// The off-screen buffer. Created on first use, sized to the panel.
M5Canvas &ui_canvas();

// True once the canvas exists. False means allocation failed and nothing can
// be drawn -- say so rather than pushing an empty sprite over a good page.
bool ui_canvas_ready();

// Sends the canvas to the panel and waits for the waveform to finish. Returns
// the refresh time in milliseconds, or -1 if there is no canvas.
//
// The wait is not optional: a battery wake deep-sleeps immediately afterwards,
// and deep sleep kills the refresh task mid-waveform.
int64_t ui_canvas_push();

#endif // UI_GFX_TARGET_HPP
