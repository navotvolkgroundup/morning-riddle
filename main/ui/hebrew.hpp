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
// `scale` multiplies every glyph, advance and space -- 2 draws at double size.
// The blob is a 24x41 bitmap with no larger cut, so this is pixel doubling, not
// a second face. It looks like exactly what it is at 2x, which on a six-colour
// panel viewed from across a room is the right trade.
void draw_line_rtl(const he_metrics_t *m, int right_x, int y, const char *s,
                   uint32_t colour = TFT_BLACK, int scale = 1);

// Width of a line in pixels, using exactly the rules draw_line_rtl draws with.
int measure(const he_metrics_t *m, const char *s, int scale = 1);

// Number of lines draw_wrapped would produce, without drawing any of them.
// The page needs this to centre a block it has not drawn yet.
int wrapped_lines(const he_metrics_t *m, int width, const char *text,
                  int max_lines);

// Draws right-to-left like draw_line_rtl, but never runs off the left edge.
//
// draw_line_rtl walks right to left and simply RETURNS when it runs out of
// width, so a line that is too long loses its tail with no warning at all.
// That is how Sunday's timetable came out as "חינוך גופ": the last subject
// fell off the panel and nothing said so.
//
// This measures first. If the line fits it is drawn unchanged. If it does not,
// whole trailing items are dropped -- at a comma where there is one, otherwise
// at a space -- and "..." is appended so the reader can see something was cut.
// Cutting mid-word is never useful; a visibly shortened list is.
void draw_line_rtl_fit(const he_metrics_t *m, int right_x, int left_limit,
                       int y, const char *s, uint32_t colour = TFT_BLACK);

// Word-wraps to the given width and draws each line right-aligned.
// Returns the y below the block, or -1 if it did not fit above `bottom`.
int draw_wrapped(const he_metrics_t *m, int y, int left_x, int right_x,
                 int bottom, const char *text, int max_lines,
                 uint32_t colour = TFT_BLACK);

}  // namespace he

#endif // UI_HEBREW_HPP
