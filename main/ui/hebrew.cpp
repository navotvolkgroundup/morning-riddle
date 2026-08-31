#include "hebrew.hpp"

#include "gfx_target.hpp"

#include <cstring>

// Blob layout, produced by gen_hebrew_fon.py (New Peninim MT, 40pt):
//   [0 .. N*kGlyph)      glyph bitmaps, 24x41 cell, 3 bytes/row, bit 0 = ink
//   [N*kGlyph .. +N)     one advance-width byte per glyph
extern const uint8_t font24HE_start[] asm("_binary_font24HE_FON_start");
extern const uint8_t font24HE_end[]   asm("_binary_font24HE_FON_end");

namespace {
constexpr int kRowBytes = 3;
constexpr int kGlyph    = kRowBytes * HE_H;

inline size_t blob_size() { return (size_t)(font24HE_end - font24HE_start); }

void draw_glyph(int x, int y, uint32_t cp, uint32_t colour, int scale)
{
    if (cp < HE_BASE || cp > HE_LAST) return;
    size_t off = (size_t)(cp - HE_BASE) * kGlyph;
    if (off + kGlyph > blob_size()) return;

    const uint8_t *g = font24HE_start + off;
    for (int row = 0; row < HE_H; row++) {
        for (int b = 0; b < kRowBytes; b++) {
            uint8_t byte = g[row * kRowBytes + b];
            if (byte == 0xFF) continue;                 // all background
            for (int bit = 0; bit < 8; bit++) {
                if ((byte >> (7 - bit)) & 1) continue;  // 1 = background
                const int px = x + (b * 8 + bit) * scale;
                const int py = y + row * scale;
                if (scale == 1) ui_canvas().drawPixel(px, py, colour);
                else ui_canvas().fillRect(px, py, scale, scale, colour);
            }
        }
    }
}
}  // namespace

namespace he {

void load_metrics(he_metrics_t *m)
{
    if (!m) return;
    std::memset(m, 0, sizeof *m);
    const size_t woff = (size_t)HE_NGLYPH * kGlyph;
    for (int i = 0; i < HE_NGLYPH; i++) {
        // A missing or truncated table falls back to the full cell width, which
        // renders wide and ugly rather than overlapping into gibberish.
        m->width[i] = (woff + (size_t)i < blob_size())
                          ? font24HE_start[woff + i]
                          : (uint8_t)HE_W;
    }
}

int measure(const he_metrics_t *m, const char *s, int scale)
{
    int w = 0;
    const char *p = s;
    uint32_t cp;
    int n;
    while ((n = he_utf8_next(p, &cp)) > 0) {
        if (he_is_letter(cp))        { w += (he_glyph_width(m, cp) + HE_GAP) * scale; p += n; }
        else if (cp == ' ')          { w += HE_SPACE * scale; p += n; }
        else if (cp > 0x20 && cp < 0x7F) {
            uint32_t c2; int k; const char *q = p; int run = 0;
            while ((k = he_utf8_next(q, &c2)) > 0 && c2 > 0x20 && c2 < 0x7F) {
                run++; q += k;
            }
            w += run * HE_LAT_W * scale;
            p = q;
        } else { p += n; }
    }
    return w;
}

void draw_line_rtl_fit(const he_metrics_t *m, int right_x, int left_limit,
                       int y, const char *s, uint32_t colour)
{
    const int avail = right_x - left_limit;
    if (avail <= 0 || !s || !*s) return;
    if (measure(m, s) <= avail) { draw_line_rtl(m, right_x, y, s, colour); return; }

    // Too wide. Walk back to the last comma (or failing that, space) whose
    // prefix plus "..." fits, so whole subjects drop rather than half a word.
    char buf[192];
    const size_t len = strlen(s);
    size_t cut = len;
    while (cut > 0) {
        size_t c = 0;
        for (size_t i = 0; i < cut; i++) if (s[i] == ',') c = i;
        if (c == 0) for (size_t i = 0; i < cut; i++) if (s[i] == ' ') c = i;
        if (c == 0) break;
        cut = c;
        if (cut + 4 >= sizeof buf) continue;
        memcpy(buf, s, cut);
        strcpy(buf + cut, "...");
        if (measure(m, buf) <= avail) { draw_line_rtl(m, right_x, y, buf, colour); return; }
    }
    // No break point short enough. Draw what fits and let draw_line_rtl clip;
    // better a partial line than a blank one.
    draw_line_rtl(m, right_x, y, s, colour);
}

void draw_line_rtl(const he_metrics_t *m, int right_x, int y, const char *s,
                   uint32_t colour, int scale)
{
    int x = right_x;
    const char *p = s;
    uint32_t cp;
    int n;

    while ((n = he_utf8_next(p, &cp)) > 0) {
        if (he_is_letter(cp)) {
            int w = he_glyph_width(m, cp);
            x -= (w + HE_GAP) * scale;
            if (x < 0) return;
            draw_glyph(x, y, cp, colour, scale);
            p += n;
        } else if (cp == ' ') {
            x -= HE_SPACE * scale;
            p += n;
        } else if (cp > 0x20 && cp < 0x7F) {
            // Reserve the whole Latin run, then draw it LTR inside that slot so
            // "NVIDIA" does not come out reversed.
            const char *run = p;
            int runlen = 0, runbytes = 0;
            const char *q = p;
            uint32_t c2; int k;
            while ((k = he_utf8_next(q, &c2)) > 0 && c2 > 0x20 && c2 < 0x7F) {
                runlen++; runbytes += k; q += k;
            }
            x -= runlen * HE_LAT_W * scale;
            if (x < 0) return;
            char buf[96];
            int cpy = runbytes < (int)sizeof(buf) - 1 ? runbytes : (int)sizeof(buf) - 1;
            std::memcpy(buf, run, cpy);
            buf[cpy] = '\0';
            // Latin sits lower in the taller Hebrew cell so baselines align.
            ui_canvas().setTextColor(colour);
            ui_canvas().setTextSize(2 * scale);
            ui_canvas().drawString(buf, x, y + (HE_H * scale - 16 * scale) / 2);
            p += runbytes;
        } else {
            p += n;                                     // undrawable: skip
        }
    }
}

int wrapped_lines(const he_metrics_t *m, int width, const char *text,
                  int max_lines)
{
    const char *p = text;
    int lines = 0;
    while (*p && lines < max_lines) {
        const int len = he_line_break(m, p, width);
        if (len <= 0) break;
        lines++;
        p += len;
        while (*p == ' ') p++;
    }
    return lines;
}

int draw_wrapped(const he_metrics_t *m, int y, int left_x, int right_x,
                 int bottom, const char *text, int max_lines, uint32_t colour)
{
    const int width = right_x - left_x;
    const char *p = text;

    for (int line = 0; *p && line < max_lines; line++) {
        int len = he_line_break(m, p, width);
        if (len <= 0) break;                            // nothing fits: stop

        char buf[256];
        if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
        std::memcpy(buf, p, len);
        buf[len] = '\0';

        if (y + HE_H > bottom) return -1;
        draw_line_rtl(m, right_x, y, buf, colour);
        y += HE_H - 6;                                  // cell has built-in slack

        p += len;
        while (*p == ' ') p++;
    }
    return y;
}

}  // namespace he
