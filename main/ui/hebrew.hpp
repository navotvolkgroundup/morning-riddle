// Hebrew rendering on the M5Paper Color.
//
// Layout (decoding, advances, word breaking) lives in core/he_text.c and is
// tested on the host. This file is only pixels: glyph blitting and the
// right-to-left pen, against M5GFX instead of the Waveshare Paint_* API.
//
// COLOUR is a parameter now. The Waveshare panel was 1-bit and every call
// hard-coded BLACK; this one has six colours, so the caller chooses. The
// default stays black -- a page that reads correctly in black on white and
// uses colour only for emphasis degrades better than one that depends on it.
//
// The font blob is embedded via EMBED_FILES in main/CMakeLists.txt.

#ifndef UI_HEBREW_HPP
#define UI_HEBREW_HPP

#include <M5Unified.h>

extern "C" {
#include "he_text.h"
}

namespace he {

// Fills `m` from the width table appended after the glyph bitmaps in the blob.
void load_metrics(he_metrics_t *m);

// Draws a mixed Hebrew/Latin line right-to-left from `right_x`.
//
// Deliberately simplified bidi, not the Unicode algorithm: the line runs RTL
// and Latin/digit runs keep their internal LTR order. Correct for the shape
// real text takes here ("מתמטיקה, אנגלית"); nested or bidirectional
// punctuation will not be perfect.
void draw_line_rtl(const he_metrics_t *m, int right_x, int y, const char *s,
                   uint32_t colour = TFT_BLACK);

// Word-wraps to the given width and draws each line right-aligned.
// Returns the y below the block, or -1 if it did not fit above `bottom`.
int draw_wrapped(const he_metrics_t *m, int y, int left_x, int right_x,
                 int bottom, const char *text, int max_lines,
                 uint32_t colour = TFT_BLACK);

}  // namespace he

#endif // UI_HEBREW_HPP
