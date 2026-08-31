#include "hebrew.hpp"

#include "gfx_target.hpp"

#include <cstring>

// Blob layout, produced by gen_hebrew_fon.py:
//   [0 .. N*glyph)      glyph bitmaps, row_bytes/row, bit 0 = ink
//   [N*glyph .. +N)     one advance-width byte per glyph
extern const uint8_t font24HE_start[] asm("_binary_font24HE_FON_start");
extern const uint8_t font24HE_end[]   asm("_binary_font24HE_FON_end");
extern const uint8_t font16HE_start[] asm("_binary_font16HE_FON_start");
extern const uint8_t font16HE_end[]   asm("_binary_font16HE_FON_end");

namespace {

// Builds a face and reads its width table. A missing or truncated table falls
// back to the full cell width, which renders wide and ugly rather than
// overlapping into gibberish.
void init_face(he::face &f, const uint8_t *start, const uint8_t *end,
               int cw, int ch, int gap, int space, int lat, int text_size)
{
    f.blob = start; f.size = (size_t)(end - start);
    f.cell_w = cw;  f.cell_h = ch;  f.row_bytes = cw / 8;
    f.gap = gap;    f.space = space;  f.lat = lat;  f.text_size = text_size;

    const size_t glyph = (size_t)f.row_bytes * f.cell_h;
    const size_t woff  = (size_t)HE_NGLYPH * glyph;
    for (int i = 0; i < HE_NGLYPH; i++)
        f.width[i] = (woff + (size_t)i < f.size) ? f.blob[woff + i] : (uint8_t)cw;
}

void draw_glyph(const he::face &f, int x, int y, uint32_t cp, uint32_t colour,
                int scale)
{
    if (cp < HE_BASE || cp > HE_LAST) return;
    const size_t glyph = (size_t)f.row_bytes * f.cell_h;
    const size_t off = (size_t)(cp - HE_BASE) * glyph;
    if (off + glyph > f.size) return;

    const uint8_t *g = f.blob + off;
    for (int row = 0; row < f.cell_h; row++) {
        for (int b = 0; b < f.row_bytes; b++) {
            const uint8_t byte = g[row * f.row_bytes + b];
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

const face &body()
{
    static face f; static bool done = false;
    // 24x41 at 40pt. HE_GAP/HE_SPACE/HE_LAT_W describe this cut and this cut
    // only -- they are what core/he_text.c and its host tests are pinned to.
    if (!done) { init_face(f, font24HE_start, font24HE_end,
                           24, 41, HE_GAP, HE_SPACE, HE_LAT_W, 2); done = true; }
    return f;
}

const face &small()
{
    static face f; static bool done = false;
    // 16x24 at 22pt. The gap and space scale with the cell; the Latin advance
    // does NOT -- it stays 12 at setTextSize(2), because M5GFX's built-in font
    // has no half sizes and 6x8 at size 1 is illegible on this panel. Latin
    // therefore reads proportionally LARGER inside the small face, which is
    // what the dateline wants: "31.08" and the issue number are the two things
    // in that line a reader actually looks for.
    if (!done) { init_face(f, font16HE_start, font16HE_end,
                           16, 24, 2, 5, 12, 2); done = true; }
    return f;
}

void load_metrics(he_metrics_t *m)
{
    if (!m) return;
    std::memcpy(m->width, body().width, sizeof m->width);
}

int measure(const face &f, const char *s, int scale)
{
    int w = 0;
    const char *p = s;
    uint32_t cp;
    int n;
    while ((n = he_utf8_next(p, &cp)) > 0) {
        if (he_is_letter(cp)) {
            w += (f.width[cp - HE_BASE] + f.gap) * scale; p += n;
        } else if (cp == ' ') {
            w += f.space * scale; p += n;
        } else if (cp > 0x20 && cp < 0x7F) {
            uint32_t c2; int k; const char *q = p; int run = 0;
            while ((k = he_utf8_next(q, &c2)) > 0 && c2 > 0x20 && c2 < 0x7F) {
                run++; q += k;
            }
            w += run * f.lat * scale;
            p = q;
        } else { p += n; }
    }
    return w;
}

void draw_line_rtl(const face &f, int right_x, int y, const char *s,
                   uint32_t colour, int scale)
{
    int x = right_x;
    const char *p = s;
    uint32_t cp;
    int n;

    while ((n = he_utf8_next(p, &cp)) > 0) {
        if (he_is_letter(cp)) {
            x -= (f.width[cp - HE_BASE] + f.gap) * scale;
            if (x < 0) return;
            draw_glyph(f, x, y, cp, colour, scale);
            p += n;
        } else if (cp == ' ') {
            x -= f.space * scale;
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
            x -= runlen * f.lat * scale;
            if (x < 0) return;
            char buf[96];
            int cpy = runbytes < (int)sizeof(buf) - 1 ? runbytes : (int)sizeof(buf) - 1;
            std::memcpy(buf, run, cpy);
            buf[cpy] = '\0';
            // Centred in the cell rather than sat on the Hebrew baseline: the
            // two faces have unrelated baselines and optical centring is what
            // reads level in a mixed line.
            const int lat_h = 8 * f.text_size * scale;
            ui_canvas().setTextColor(colour);
            ui_canvas().setTextSize(f.text_size * scale);
            ui_canvas().drawString(buf, x, y + (f.cell_h * scale - lat_h) / 2);
            p += runbytes;
        } else {
            p += n;                                     // undrawable: skip
        }
    }
}

void draw_line_rtl_fit(const face &f, int right_x, int left_limit,
                       int y, const char *s, uint32_t colour)
{
    const int avail = right_x - left_limit;
    if (avail <= 0 || !s || !*s) return;
    if (measure(f, s) <= avail) { draw_line_rtl(f, right_x, y, s, colour); return; }

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
        if (measure(f, buf) <= avail) { draw_line_rtl(f, right_x, y, buf, colour); return; }
    }
    // No break point short enough. Draw what fits and let draw_line_rtl clip;
    // better a partial line than a blank one.
    draw_line_rtl(f, right_x, y, s, colour);
}

int he_line_break_face(const face &f, const char *s, int width)
{
    int w = 0;
    const char *p = s;
    const char *last_space = nullptr;
    uint32_t cp;
    int n;
    while ((n = he_utf8_next(p, &cp)) > 0) {
        int adv = 0;
        if (he_is_letter(cp))            adv = f.width[cp - HE_BASE] + f.gap;
        else if (cp == ' ')            { adv = f.space; last_space = p; }
        else if (cp > 0x20 && cp < 0x7F) adv = f.lat;
        if (w + adv > width) {
            // Break at the last space, so a word is never split. With no space
            // to fall back on, break here rather than overrun the margin.
            return (int)((last_space ? last_space : p) - s);
        }
        w += adv;
        p += n;
    }
    return (int)(p - s);
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
        draw_line_rtl(body(), right_x, y, buf, colour);
        y += HE_H - 6;                                  // cell has built-in slack

        p += len;
        while (*p == ' ') p++;
    }
    return y;
}

}  // namespace he
