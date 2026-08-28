// Hebrew text layout: UTF-8 decoding, advance widths, and line breaking.
//
// Pure. No drawing, no board, no IDF. The Waveshare version kept all of this
// inside hebrew.inc alongside the pixel blitting, which meant the word-wrap --
// the part most likely to be subtly wrong -- could only be checked by looking
// at a panel. On this board looking at the panel costs 17 SECONDS per attempt.
//
// Glyph widths come from the caller, so tests can use a synthetic table and the
// firmware can use the real font blob.

#ifndef HE_TEXT_H
#define HE_TEXT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HE_W        24                  // cell width (max glyph width)
#define HE_H        41                  // cell height
#define HE_BASE     0x05D0
#define HE_LAST     0x05EA
#define HE_NGLYPH   (HE_LAST - HE_BASE + 1)
#define HE_GAP      3                   // space between letters
#define HE_SPACE    9                   // width of a word space
#define HE_LAT_W    16                  // Latin advance, for embedded runs

typedef struct {
    uint8_t width[HE_NGLYPH];           // ink width per Hebrew letter
} he_metrics_t;

// Anywhere in the Hebrew block -- including niqqud and punctuation we have no
// glyph for.
static inline bool he_is_hebrew(uint32_t cp) { return cp >= 0x0590 && cp <= 0x05FF; }

// A letter this font can actually draw. NOT the same as he_is_hebrew, and the
// difference is a real bug the Waveshare renderer shipped: a niqqud mark is in
// the Hebrew block but outside the glyph range, so it advanced the pen by
// HE_GAP and drew nothing -- an invisible gap in the middle of a word.
static inline bool he_is_letter(uint32_t cp) { return cp >= HE_BASE && cp <= HE_LAST; }

// Decodes one UTF-8 sequence. Returns bytes consumed, 0 at end of string.
int he_utf8_next(const char *s, uint32_t *cp);

// Ink width of one Hebrew letter, or HE_W if the table has nothing usable.
int he_glyph_width(const he_metrics_t *m, uint32_t cp);

// How far the pen moves after this codepoint. Undrawable codepoints advance
// zero -- they are skipped rather than shown as a gap.
int he_advance(const he_metrics_t *m, uint32_t cp);

// Total advance of a string.
int he_measure(const he_metrics_t *m, const char *s);

// Bytes of `s` that fit in `width`, broken at a space where possible.
//
// Always makes progress on a non-empty string with a positive width: a word
// too wide for the line is returned WHOLE and left to overhang, because
// returning nothing would make the caller stop and silently drop the rest of
// the text. Returns 0 only for an empty string or a non-positive width.
int he_line_break(const he_metrics_t *m, const char *s, int width);

#ifdef __cplusplus
}
#endif

#endif // HE_TEXT_H
