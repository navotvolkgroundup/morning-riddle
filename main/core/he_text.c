// Hebrew text layout. See he_text.h for why this is separate from drawing.

#include "he_text.h"

#include <stddef.h>

int he_utf8_next(const char *s, uint32_t *cp)
{
    unsigned char c = (unsigned char)s[0];
    if (!c) return 0;
    if (c < 0x80)          { *cp = c;                       return 1; }
    if ((c & 0xE0) == 0xC0 && s[1]) {
        *cp = ((uint32_t)(c & 0x1F) << 6) | (s[1] & 0x3F);  return 2;
    }
    if ((c & 0xF0) == 0xE0 && s[1] && s[2]) {
        *cp = ((uint32_t)(c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    }
    if ((c & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) { *cp = 0xFFFD; return 4; }
    *cp = 0xFFFD;                                           return 1;
}

int he_glyph_width(const he_metrics_t *m, uint32_t cp)
{
    if (!m || cp < HE_BASE || cp > HE_LAST) return 0;
    int w = m->width[cp - HE_BASE];
    return (w > 0 && w <= HE_W) ? w : HE_W;
}

int he_advance(const he_metrics_t *m, uint32_t cp)
{
    if (cp == ' ')                   return HE_SPACE;
    if (he_is_letter(cp))            return he_glyph_width(m, cp) + HE_GAP;
    if (he_is_hebrew(cp))            return 0;   // in-block but no glyph
    if (cp >= 0x21 && cp < 0x7F)     return HE_LAT_W;
    return 0;                        // not drawable, not counted
}

int he_measure(const he_metrics_t *m, const char *s)
{
    uint32_t cp; int n, w = 0;
    if (!s) return 0;
    while ((n = he_utf8_next(s, &cp)) > 0) { s += n; w += he_advance(m, cp); }
    return w;
}

int he_line_break(const he_metrics_t *m, const char *s, int width)
{
    if (!s || !*s || width <= 0) return 0;

    const char *q = s;
    const char *last_space = NULL;
    int w = 0;
    uint32_t cp; int n;

    while ((n = he_utf8_next(q, &cp)) > 0) {
        int adv = he_advance(m, cp);
        if (w + adv > width) break;
        if (cp == ' ') last_space = q;
        w += adv;
        q += n;
    }

    // Break at the last space, but only if that leaves something behind.
    if (*q && last_space && last_space > s) return (int)(last_space - s);

    // NOTHING FIT. A word wider than the whole line has no space to break at,
    // and returning 0 here would make the caller stop -- silently dropping the
    // rest of the text, which for a riddle means the question just ends. Emit
    // the over-wide word whole instead and let it overhang: draw_line_rtl
    // clips at x < 0, so the result is visibly too long rather than missing.
    if (q == s) {
        while ((n = he_utf8_next(q, &cp)) > 0 && cp != ' ') q += n;
        if (q == s && n > 0) q += n;        // guarantee forward progress
    }

    return (int)(q - s);
}
