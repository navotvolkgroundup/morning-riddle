#include "page_daily.hpp"

#include <M5Unified.h>

#include "gfx_target.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hebrew.hpp"

namespace {

// Three choices at 52 plus an 8px gap is 172; a two-line question is 82. That
// is 254 against the 307 the worst-case layout now leaves.
constexpr int kChoiceH   = 52;
constexpr int kChoiceGap = 8;

// The paper's name lives in core/masthead.h, where a host test can measure it
// against the widest dateline the page can produce. See that file for why it
// is not "חידת הבוקר" any more.

// The answer is drawn at double size. See the draw site for why, and hebrew.hpp
// for what pixel doubling a 24x41 blob actually looks like.
constexpr int kAnswerScale = 2;

// Only for kinds that are NOT riddles; see the draw site.
const char *kind_label(uint8_t kind)
{
    switch (kind) {
    case RK_JOKE: return "\xd7\x91\xd7\x93\xd7\x99\xd7\x97\xd7\x94";                                  // "joke"
    case RK_WORD: return "\xd7\x9e\xd7\x99\xd7\x9c\xd7\x94 \xd7\xa9\xd7\x9c \xd7\x94\xd7\x99\xd7\x95\xd7\x9d";  // "word of the day"
    case RK_MATH: return "\xd7\xaa\xd7\xa8\xd7\x92\xd7\x99\xd7\x9c";                                  // "exercise"
    default:      return nullptr;
    }
}

he_metrics_t g_metrics;
bool g_metrics_loaded = false;

const he_metrics_t *metrics()
{
    if (!g_metrics_loaded) { he::load_metrics(&g_metrics); g_metrics_loaded = true; }
    return &g_metrics;
}

// The paper's name, alone on the top line, in the one colour a masthead has
// ever been printed in besides black.
void draw_nameplate()
{
    he::draw_line_rtl(he::body(), DL_CANVAS_W - DL_MARGIN_X, 0, MASTHEAD_NAME,
                      TFT_RED);
}

// The dateline, reversed out of a solid bar.
//
// White type on black is the strongest device this panel has and the page was
// not using it. It is also what turned a heading into a masthead: the name had
// the dateline beside it on one line, which is a title with a subtitle, where a
// paper puts its name alone and a strip of furniture underneath.
void draw_dateline(const page_daily_content &c)
{
    ui_canvas().fillRect(DL_MARGIN_X, DL_BAR_Y, DL_CANVAS_W - 2 * DL_MARGIN_X,
                         DL_BAR_H, TFT_BLACK);

    char dl[64];
    int n = std::snprintf(dl, sizeof dl, "%s \xc2\xb7 %s",
                          schedule_weekday_he(schedule_weekday(c.today)),
                          c.date ? c.date : "");
    // The issue number, and it is a real issue number: mornings published,
    // never reset. Below two it says nothing -- "issue 1" is not a boast.
    if (c.issue > 1 && n > 0 && n < (int)sizeof dl)
        std::snprintf(dl + n, sizeof dl - n,
                      " \xc2\xb7 \xd7\x92\xd7\x99\xd7\x9c\xd7\x99\xd7\x95\xd7\x9f %u",  // "issue N"
                      (unsigned)c.issue);

    he::draw_line_rtl(he::small(), DL_CANVAS_W - DL_MARGIN_X - 8,
                      DL_BAR_Y + (DL_BAR_H - 24) / 2, dl, TFT_WHITE);
}

// The weather symbol, drawn rather than blitted.
//
// wmo_icon() names PNGs that live in the Waveshare tree and were never ported.
// Six colours make a drawn sun better than a 1-bit one anyway: a yellow disc
// with rays and a grey cloud is a dozen primitives and it spends inks the page
// otherwise wastes.
void draw_wx_icon(int cx, int cy, weather_icon_e kind)
{
    auto &g = ui_canvas();
    const bool sunny = (kind == WX_ICON_SUN || kind == WX_ICON_PART);
    const bool cloudy = (kind != WX_ICON_SUN);

    if (sunny) {
        g.fillCircle(cx, cy, 13, TFT_YELLOW);
        // Eight rays, by octant, so no trigonometry and no float on a path
        // that runs twice a day.
        static const int8_t dx[8] = { 1, 1, 0, -1, -1, -1,  0,  1 };
        static const int8_t dy[8] = { 0, 1, 1,  1,  0, -1, -1, -1 };
        for (int i = 0; i < 8; i++) {
            const int x0 = cx + dx[i] * 17, y0 = cy + dy[i] * 17;
            const int x1 = cx + dx[i] * 23, y1 = cy + dy[i] * 23;
            g.drawLine(x0, y0, x1, y1, TFT_YELLOW);
            g.drawLine(x0 + 1, y0, x1 + 1, y1, TFT_YELLOW);
        }
    }
    if (cloudy) {
        // Storm and overcast get a black cloud; the rest a grey one, so the
        // symbol carries severity even before the colour is read.
        const uint32_t col = (kind == WX_ICON_CLOUD || kind == WX_ICON_STORM)
                                 ? TFT_BLACK : TFT_DARKGREY;
        const int ox = (kind == WX_ICON_PART) ? 8 : 0;
        const int oy = (kind == WX_ICON_PART) ? 6 : 0;
        g.fillCircle(cx - 9 + ox, cy + 5 + oy, 11, col);
        g.fillCircle(cx + 5 + ox, cy - 1 + oy, 13, col);
        g.fillRect(cx - 14 + ox, cy + 4 + oy, 28, 11, col);
    }
    if (kind == WX_ICON_RAIN)
        for (int i = 0; i < 3; i++)
            g.drawLine(cx - 12 + i * 11, cy + 18, cx - 16 + i * 11, cy + 28, TFT_BLUE);
    if (kind == WX_ICON_SNOW)
        for (int i = 0; i < 3; i++)
            g.fillCircle(cx - 12 + i * 11, cy + 22, 3, TFT_BLUE);
    if (kind == WX_ICON_STORM) {
        g.fillTriangle(cx + 2, cy + 16, cx - 6, cy + 31, cx + 3, cy + 29, TFT_YELLOW);
        g.fillTriangle(cx + 3, cy + 29, cx - 2, cy + 41, cx + 8, cy + 24, TFT_YELLOW);
    }
    if (kind == WX_ICON_FOG)
        for (int i = 0; i < 3; i++)
            g.fillRect(cx - 20, cy + 14 + i * 7, 40, 3, TFT_DARKGREY);
}

// The boxed weather panel: symbol, temperature, advice.
//
// A BOX, NOT A LINE. As a line the weather was one more row of the same text
// at the same size as the timetable, and the temperature -- the most scannable
// thing on the page -- was the smallest. Boxed beside the timetable it becomes
// a panel a child reads at a glance, and the band goes from two stacked
// captions to something with a shape.
void draw_weather(const page_daily_content &c, int y)
{
    if (!c.wx) return;
    auto &g = ui_canvas();

    const int x0 = DL_MARGIN_X, x1 = DL_MARGIN_X + DL_WXBOX_W;
    const int cx = (x0 + x1) / 2;
    g.drawRect(x0, y, DL_WXBOX_W, DL_WXBOX_H, TFT_BLACK);

    draw_wx_icon(cx, y + 26, weather_icon(c.wx->wmo));

    // Whole degrees, big, with the ring drawn rather than typed: the degree
    // sign is U+00B0 and neither the Hebrew blob nor M5GFX's built-in font is
    // guaranteed to have it, and a missing glyph here is a silent blank.
    const int t10 = c.wx->temp_x10;
    char num[8];
    std::snprintf(num, sizeof num, "%d", (t10 + (t10 < 0 ? -5 : 5)) / 10);
    g.setTextColor(TFT_BLACK);
    g.setTextSize(4);                        // 6x8 cell -> 24x32
    const int nw = (int)std::strlen(num) * 24;
    g.drawString(num, cx - (nw + 9) / 2, y + 52);
    g.drawCircle(cx - (nw + 9) / 2 + nw + 5, y + 56, 4, TFT_BLACK);

    // Stale outranks tone: a reading that may be wrong must not be dressed in
    // the colour that says "trust this and take a hat".
    uint32_t colour = TFT_BLACK;
    if (weather_is_stale(c.wx, c.now_utc)) {
        colour = TFT_RED;
    } else {
        switch (weather_advice_tone(c.wx)) {
        case WX_TONE_WET:  colour = TFT_BLUE;   break;
        case WX_TONE_COLD: colour = TFT_BLUE;   break;
        case WX_TONE_HOT:  colour = TFT_YELLOW; break;
        default:           colour = TFT_BLACK;  break;
        }
    }
    // Centred in the box, so it reads as the panel's caption rather than as a
    // line of the page that happens to start there.
    const char *adv = weather_advice_he(c.wx);
    const int aw = he::measure(he::small(), adv);
    he::draw_line_rtl(he::small(), cx + aw / 2, y + DL_WXBOX_H - 26, adv, colour);
}

}  // namespace

void page_daily_draw(const page_daily_content &c)
{
    const he_metrics_t *m = metrics();

    // What is present decides the layout; the layout decides where it goes.
    daily_flags_t f;
    f.schedule = c.sched && schedule_for_day(c.sched, c.today)[0] != 0;
    f.weather  = c.wx && c.wx->fetched_at != 0;
    f.callout  = c.turn_kid >= 0 && c.kids && c.turn_kid < c.kids->count;
    f.birthday = c.kids && kids_birthday_on(c.kids, c.month, c.day) >= 0;

    daily_layout_t L;
    daily_layout(&f, &L);

    ui_canvas().startWrite();
    ui_canvas().fillScreen(TFT_WHITE);

    draw_nameplate();
    draw_dateline(c);

    // Hairlines top and bottom. A bordered rectangle around the whole band
    // made it look like a form to be filled in; two rules make it look like a
    // column to be read, and the weather panel inside now supplies the only
    // box on the page, which is what makes that box mean something.
    if (L.band_h > 0) {
        const int w = DL_CANVAS_W - 2 * DL_MARGIN_X;
        ui_canvas().fillRect(DL_MARGIN_X, L.band_y, w, DL_HAIR, TFT_BLACK);
        ui_canvas().fillRect(DL_MARGIN_X, L.band_y + L.band_h - DL_HAIR, w,
                             DL_HAIR, TFT_BLACK);
    }

    if (L.weather_y != DL_ABSENT) draw_weather(c, L.weather_y);

    // The timetable wraps in what the panel leaves, which is 232px and two
    // lines -- more room in total than the single full-width line it used to
    // get, and the line that kept eliding subjects.
    if (L.schedule_y != DL_ABSENT) {
        const int left = (L.weather_y != DL_ABSENT)
                             ? DL_MARGIN_X + DL_WXBOX_W + 12 : DL_MARGIN_X;
        he::draw_wrapped(m, L.schedule_y, left, DL_CANVAS_W - DL_MARGIN_X,
                         DL_BODY_BOTTOM, schedule_for_day(c.sched, c.today),
                         DL_SCHED_LINES);
    }

    // A small standing head over the name, instead of two body lines saying
    // the same thing at the same weight. The label is the same every year; the
    // name is the news.
    if (L.birthday_y != DL_ABSENT) {
        const int who = kids_birthday_on(c.kids, c.month, c.day);
        he::draw_line_rtl(he::small(), DL_CANVAS_W - DL_MARGIN_X,
                          L.birthday_label_y,
                          "\xd7\x99\xd7\x95\xd7\x9d \xd7\x94\xd7\x95\xd7\x9c\xd7\x93\xd7\xaa",  // "birthday"
                          TFT_RED);
        if (who >= 0)
            he::draw_line_rtl(he::body(), DL_CANVAS_W - DL_MARGIN_X,
                              L.birthday_y, c.kids->kid[who].name, TFT_RED);
    }

    // WHOSE TURN IT IS, EVERY MORNING, IN ROTATION. This used to fire on
    // roughly one day in three and pick by hash, so a guess belonged to nobody.
    // Now the page names one child a day and carries their own run of turns on
    // the flag side -- the same nameplate-and-dateline treatment as the
    // masthead, which is what makes the two lines read as one paper.
    if (L.callout_y != DL_ABSENT && c.turn_kid >= 0) {
        char line[KID_NAME_MAX + 24];
        std::snprintf(line, sizeof line, "%s, \xd7\x96\xd7\x90\xd7\xaa "
                      "\xd7\x91\xd7\xa9\xd7\x91\xd7\x99\xd7\x9c\xd7\x9a",
                      c.kids->kid[c.turn_kid].name);
        he::draw_line_rtl_fit(he::body(), DL_CANVAS_W - DL_MARGIN_X,
                              DL_MARGIN_X, L.callout_y, line);

        // Counted in turns, not days -- see riddle_decide.h. Below two there
        // is nothing to say, and saying "1 in a row" to a child who has just
        // started is worse than saying nothing.
        if (c.turn_streak > 1) {
            char run[40];
            std::snprintf(run, sizeof run,
                          "%u \xd7\xa4\xd7\xa2\xd7\x9e\xd7\x99\xd7\x9d "
                          "\xd7\x91\xd7\xa8\xd7\xa6\xd7\xa3",     // "N times in a row"
                          (unsigned)c.turn_streak);
            he::draw_line_rtl(he::small(),
                              DL_MARGIN_X + he::measure(he::small(), run),
                              L.callout_y + 9, run);
        }
    }

    // The rule above the lead. Thinner than the masthead's, thicker than the
    // band's hairlines: three weights is how the page says "major, minor,
    // minor" without a fourth type size.
    ui_canvas().fillRect(DL_MARGIN_X, L.lead_rule_y,
                         DL_CANVAS_W - 2 * DL_MARGIN_X, DL_LEAD_RULE_H,
                         TFT_BLACK);

    // THE RIDDLE BLOCK, CENTRED IN WHAT IS LEFT.
    //
    // It used to start hard against riddle_top and flow down, so the
    // composition was decided by how long the riddle happened to be: a short
    // one left the bottom of the panel blank and looked unfinished, a long one
    // filled it and looked deliberate. Measuring first and centring makes both
    // look composed.
    const int riddle_w = DL_CANVAS_W - 2 * DL_MARGIN_X;
    const char *q = c.question ? c.question : "";

    // THE DROP CAP, and the one thing that had to be right about it: it is the
    // first LETTER of a word, so any real gap after it makes the remainder
    // read as a separate token. Hebrew "מה" broken as "מ ה" is not a
    // typographic device, it is a spelling mistake. Two pixels, no more.
    char cap[8] = {0};
    int cap_w = 0;
    {
        uint32_t cp;
        const int n = he_utf8_next(q, &cp);
        // Only Hebrew letters get dropped. A question opening on a digit or a
        // quote would give a cap that reads as a stray mark.
        if (n > 0 && n < (int)sizeof cap && he_is_letter(cp)) {
            std::memcpy(cap, q, (size_t)n);
            cap_w = he::measure(he::body(), cap, kAnswerScale) + 2;
            q += n;
            while (*q == ' ') q++;
        }
    }

    // Line 1 is narrowed by the cap; the rest run full width.
    const int first_w = riddle_w - cap_w;
    const int first_len = cap_w ? he_line_break(m, q, first_w) : 0;
    const char *tail = q + first_len;
    while (*tail == ' ') tail++;
    const int tail_lines = he::wrapped_lines(m, riddle_w, tail, 4);
    const int q_lines = (cap_w ? 1 : he::wrapped_lines(m, riddle_w, q, 5))
                        + (cap_w ? tail_lines : 0);

    const char *kicker = kind_label(c.kind);
    const int kicker_h = kicker ? DL_SMALL_H + 6 : 0;

    const char *why = (c.why && c.why[0]) ? c.why : nullptr;
    const int why_lines = (c.show_answer && why)
                              ? he::wrapped_lines(m, riddle_w, why, 3) : 0;

    int block_h = kicker_h + q_lines * (HE_H - 6);
    if (c.show_answer && c.answer) {
        block_h += 16 + DL_HAIR + 8 + HE_H * kAnswerScale;
        if (why_lines) block_h += 14 + why_lines * (HE_H - 6);
    } else if (c.has_choices) {
        // Each choice sits under its own hairline, and one closes the list.
        block_h += 10 + 3 * (DL_HAIR + 9 + DL_LINE_H + 9) + DL_HAIR;
    }

    // TODAY'S PICTURE, in whatever the block is not using. Six colours, which
    // is the whole gamut and the first time this page has used more than two.
    //
    // Drawn a pixel at a time into the RAM canvas: 36,000 drawPixel calls for
    // a 400x90 strip, which sounds careless and costs single-digit
    // milliseconds against a seventeen-second panel refresh. A row-packing
    // optimisation here would be measuring the wrong thing.
    const int image_h = c.image ? (int)c.image->h : 0;
    const int image_y = daily_image_in_slack(L.riddle_top, L.riddle_h,
                                             block_h, image_h);
    if (image_y != DL_ABSENT) {
        static const uint32_t kInk[STRIP_INK_COUNT] = {
            TFT_BLACK, TFT_WHITE, TFT_RED, TFT_YELLOW, TFT_BLUE, TFT_GREEN,
        };
        for (int row = 0; row < image_h; row++) {
            for (int x = 0; x < STRIP_W; x++) {
                const uint8_t ink = strip_at(c.image, x, row);
                // White is the page. Skipping it is what lets an illustration
                // sit on the paper rather than in a box.
                if (ink == STRIP_WHITE) continue;
                ui_canvas().drawPixel(x, image_y + row, kInk[ink]);
            }
        }
    }

    // The block centres in what is left after the picture, so a page with one
    // does not look bottom-heavy and a page without one is unchanged.
    const int taken = (image_y != DL_ABSENT) ? image_h + 2 * DL_ZONE_GAP : 0;
    const int slack = DL_BODY_BOTTOM - L.riddle_top - block_h - taken;
    int y = L.riddle_top + taken + (slack > 0 ? slack / 2 : 0);

    if (kicker) {
        he::draw_line_rtl(he::small(), DL_CANVAS_W - DL_MARGIN_X, y, kicker,
                          TFT_RED);
        y += kicker_h;
    }

    if (cap_w) {
        // Lifted six pixels so the doubled cell's ink sits on the first line's
        // baseline rather than hanging below it.
        he::draw_line_rtl(he::body(), DL_CANVAS_W - DL_MARGIN_X, y - 6, cap,
                          TFT_RED, kAnswerScale);
        char buf[192];
        int n = first_len < (int)sizeof buf - 1 ? first_len : (int)sizeof buf - 1;
        std::memcpy(buf, q, (size_t)n);
        buf[n] = '\0';
        he::draw_line_rtl(he::body(), DL_CANVAS_W - DL_MARGIN_X - cap_w, y, buf);
        y += HE_H - 6;
        y = he::draw_wrapped(m, y, DL_MARGIN_X, DL_CANVAS_W - DL_MARGIN_X,
                             DL_BODY_BOTTOM, tail, 4);
    } else {
        y = he::draw_wrapped(m, y, DL_MARGIN_X, DL_CANVAS_W - DL_MARGIN_X,
                             DL_BODY_BOTTOM, q, 5);
    }
    if (y < 0) y = L.riddle_top + 3 * (HE_H - 6);   // clamped; drew what it could

    if (c.show_answer && c.answer) {
        y += 16;
        ui_canvas().fillRect(DL_MARGIN_X, y, DL_CANVAS_W - 2 * DL_MARGIN_X,
                             DL_HAIR, TFT_BLACK);
        y += DL_HAIR + 8;
        // Double size, in red: the payoff the whole day builds to, and it
        // should be readable across a room.
        he::draw_line_rtl(he::body(), DL_CANVAS_W - DL_MARGIN_X, y, c.answer,
                          TFT_RED, kAnswerScale);
        y += HE_H * kAnswerScale;

        if (why_lines) {
            y += 14;
            y = he::draw_wrapped(m, y, DL_MARGIN_X, DL_CANVAS_W - DL_MARGIN_X,
                                 DL_BODY_BOTTOM, why, 3);
            // The end mark. A filled square closing the story is the oldest
            // piece of newspaper furniture there is, and it tells a child the
            // page has finished rather than run out of room.
            if (y > 0)
                ui_canvas().fillRect(DL_MARGIN_X, y - 30, 11, 11, TFT_BLACK);
        }
    } else if (c.has_choices) {
        y += 10;
        for (int i = 0; i < 3; i++) {
            if (y + DL_LINE_H + 18 > DL_BODY_BOTTOM) break;
            // A rule above each choice, and one below the last. Ruled lists
            // are how a paper prints options, and the rules also do the job
            // the deleted A/B/C markers were supposed to do: they separate
            // three lines into three things you can point at.
            ui_canvas().fillRect(DL_MARGIN_X, y, DL_CANVAS_W - 2 * DL_MARGIN_X,
                                 DL_HAIR, TFT_BLACK);
            y += DL_HAIR + 9;
            he::draw_line_rtl(he::body(), DL_CANVAS_W - DL_MARGIN_X, y,
                              c.choices[i] ? c.choices[i] : "");
            y += DL_LINE_H + 9;
        }
        ui_canvas().fillRect(DL_MARGIN_X, y, DL_CANVAS_W - 2 * DL_MARGIN_X,
                             DL_HAIR, TFT_BLACK);
    }

    ui_canvas().endWrite();
}
