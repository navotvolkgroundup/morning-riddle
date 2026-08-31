// Hebrew rendering on the M5Paper Color.
//
// Layout of BODY text (decoding, advances, word breaking) lives in
// core/he_text.c and is tested on the host. This file is only pixels: glyph
// blitting and the right-to-left pen, against M5GFX instead of the Waveshare
// Paint_* API.
//
// TWO FACES, WHICH IS WHAT MAKES THIS A PAGE AND NOT A LIST.
//
// There was one 24x41 face and one size, so every element on the page --
// masthead, dateline, timetable, question, choices -- carried identical
// weight and nothing could be subordinate to anything. Newspaper hierarchy is
// built out of small type: datelines, standing heads, kickers. Pixel doubling
// only goes up, so a second, smaller cut had to be generated.
//
//   body   24x41 @ 22pt   the page's voice
//   small  16x24 @ 20pt   dateline, labels -- 58% of body
//   body at scale 2       82px, the revealed answer only
//
// Both are New Peninim MT through gen_hebrew_fon.py, the same generator and
// the same face, so they are one family rather than two fonts. 16x24 was
// chosen over 16x22: the smaller cut's stems start to break up, and the
// generator's own notes warn that thin stems are the wrong trade on 1-bit
// e-ink where there is no antialiasing to carry them.
//
// COLOUR is a parameter. The Waveshare panel was 1-bit and every call
// hard-coded BLACK; this one has six colours, so the caller chooses. The
// default stays black -- a page that reads correctly in black on white and
// uses colour only for emphasis degrades better than one that depends on it.
//
// Both blobs are embedded via EMBED_FILES in main/CMakeLists.txt.

#ifndef UI_HEBREW_HPP
#define UI_HEBREW_HPP

#include <M5Unified.h>

#include <cstddef>
#include <cstdint>

extern "C" {
#include "he_text.h"
}

namespace he {

// One cut of the font: the blob plus the metrics that go with that cell size.
//
// The numbers are per-face rather than the #defines in he_text.h, because
// those describe the body cut only and are what the host tests pin. A face is
// built once, lazily, from the embedded blob.
struct face {
    const uint8_t *blob;
    size_t         size;
    int cell_w, cell_h, row_bytes;
    int gap;            // between letters
    int space;          // a word space
    int lat;            // advance for an embedded Latin/digit run
    int text_size;      // M5GFX setTextSize() for that run
    uint8_t width[HE_NGLYPH];
};

const face &body();
const face &small();

// Fills `m` from the BODY face, for core's word-breaking. Small text is never
// wrapped -- it is a dateline and a label, both single short lines.
void load_metrics(he_metrics_t *m);

// Draws a mixed Hebrew/Latin line right-to-left from `right_x`.
//
// Deliberately simplified bidi, not the Unicode algorithm: the line runs RTL
// and Latin/digit runs keep their internal LTR order. Correct for the shape
// real text takes here ("מתמטיקה, אנגלית"); nested or bidirectional
// punctuation will not be perfect.
//
// `scale` multiplies every glyph, advance and space -- 2 draws at double size.
// Neither blob has a larger cut, so this is pixel doubling, not a second face.
// It looks like exactly what it is up close, and from across a room that is
// the right trade.
void draw_line_rtl(const face &f, int right_x, int y, const char *s,
                   uint32_t colour = TFT_BLACK, int scale = 1);

// Width of a line in pixels, using exactly the rules draw_line_rtl draws with.
int measure(const face &f, const char *s, int scale = 1);

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
void draw_line_rtl_fit(const face &f, int right_x, int left_limit,
                       int y, const char *s, uint32_t colour = TFT_BLACK);

// Where to break a line for a given face.
//
// core's he_line_break() is pinned to the BODY cut -- its HE_GAP, HE_SPACE and
// HE_LAT_W are that cut's constants and the host tests are written against
// them. The fact of the day is set in the small face and does need wrapping,
// so this is the same walk with the face's own metrics.
int he_line_break_face(const face &f, const char *s, int width);

// Number of lines draw_wrapped would produce, without drawing any of them.
// The page needs this to centre a block it has not drawn yet. Body face only.
int wrapped_lines(const he_metrics_t *m, int width, const char *text,
                  int max_lines);

// Word-wraps to the given width and draws each line right-aligned, in body.
// Returns the y below the block, or -1 if it did not fit above `bottom`.
int draw_wrapped(const he_metrics_t *m, int y, int left_x, int right_x,
                 int bottom, const char *text, int max_lines,
                 uint32_t colour = TFT_BLACK);

}  // namespace he

#endif // UI_HEBREW_HPP
