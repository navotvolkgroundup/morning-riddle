// Which grey level produces which ink.
//
// THE PANEL IS GREYSCALE, THROUGH THIS DRIVER. M5GFX's Panel_EPD forces
// grayscale_8bit in setColorDepth() and has no colour concept anywhere -- the
// same is true at 0.2.28, so it is not a version we are behind on. Its LUT
// comment says so outright: "LUTの横軸は色の濃さ。左端が黒、右端が白の16段階
// のグレースケール" -- the horizontal axis is density, black at one end and
// white at the other, sixteen levels.
//
// And yet the panel shows yellow and blue. Spectra 6 has six pigments, and
// driving them with a greyscale waveform lands some levels on a coloured
// particle rather than between black and white. So colour on this display is
// real, and it is NOT addressable by name: TFT_RED asks for a luminance, and
// whichever pigment that luminance settles on is what you get. It has been
// coming out green.
//
// So it gets measured rather than guessed. This draws all sixteen levels with
// their index beside them; one photograph gives the whole mapping, and the
// page's palette is then defined from what the panel actually does.
//
// Build with -DPD_CALIBRATE=1 to draw this instead of the daily page.

#ifndef UI_CALIBRATE_HPP
#define UI_CALIBRATE_HPP

void calibrate_draw();

#endif // UI_CALIBRATE_HPP
