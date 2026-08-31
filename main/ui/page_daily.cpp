#include "page_daily.hpp"

#include <M5Unified.h>

#include "gfx_target.hpp"
#include <cstdio>
#include <cstdlib>

#include "hebrew.hpp"

namespace {

// Three choices at 56 plus an 8px gap is 192; a two-line question is 82. That
// is 274 against the 291 the worst-case layout leaves, which is why
// DL_RIDDLE_MIN_H is 280 and not a rounder, more comfortable number.
constexpr int kChoiceH   = 56;
constexpr int kChoiceGap = 8;

// The answer is drawn at double size. See the draw site for why, and hebrew.hpp
// for what pixel doubling a 24x41 blob actually looks like.
constexpr int kAnswerScale = 2;

he_metrics_t g_metrics;
bool g_metrics_loaded = false;

const he_metrics_t *metrics()
{
    if (!g_metrics_loaded) { he::load_metrics(&g_metrics); g_metrics_loaded = true; }
    return &g_metrics;
}

void draw_header(const page_daily_content &c)
{
    ui_canvas().setTextColor(TFT_BLACK);
    ui_canvas().setTextSize(2);
    if (c.date) ui_canvas().drawString(c.date, DL_MARGIN_X, DL_HDR_Y);

    // THE DAY NAME, IN HEBREW, AT THE RIGHT -- where an RTL page begins.
    //
    // Drawn at y=0 rather than DL_HDR_Y, and that is not a fudge: ink occupies
    // rows 5..37 of the 41px cell, so a cell at y=0 puts its ink at 5..37 and
    // clears the rule at DL_HDR_RULE_Y=40 by two pixels. That is what makes
    // this free -- the header keeps its 40px and the riddle budget below is
    // untouched, which matters because the worst-case layout (timetable,
    // weather, birthday and callout together) already sits 11px above
    // DL_RIDDLE_MIN_H. A taller header would have broken it on birthdays.
    const int wd_right = DL_CANVAS_W - DL_MARGIN_X;
    const char *wd = schedule_weekday_he(schedule_weekday(c.today));
    he::draw_line_rtl(metrics(), wd_right, 0, wd);

    // The streak is address, not information -- the page telling its reader it
    // has been paying attention. Below two days there is nothing to say.
    // In Hebrew now: "4 days" was English on a page for children who are
    // learning to read Hebrew, printed in the one spot meant to speak to them.
    if (c.streak > 1) {
        char s[32];
        std::snprintf(s, sizeof s, "%u \xd7\x99\xd7\x9e\xd7\x99\xd7\x9d",   // "N days"
                      (unsigned)c.streak);
        // Right-aligned against the day name's left edge, measured rather than
        // guessed, so a long day name ("Wednesday") cannot collide with it.
        he::draw_line_rtl(metrics(), wd_right - he::measure(metrics(), wd) - 14, 0, s);
    }

    ui_canvas().drawFastHLine(DL_MARGIN_X, DL_HDR_RULE_Y,
                             DL_CANVAS_W - 2 * DL_MARGIN_X, TFT_BLACK);
}

void draw_weather(const page_daily_content &c, int y)
{
    if (!c.wx) return;

    // WHOLE DEGREES, AND HEBREW. This line used to read "19.4C partly cloudy
    // 24/17", which is four facts and no decision. The tenth of a degree was
    // never actionable, the English label was unreadable to the audience, and
    // the high/low asked the reader to do the inference themselves. What
    // replaces them is the inference: the temperature they are walking out
    // into, and what to put on.
    //
    // Rounded rather than truncated -- 19.6C shown as 19 is wrong by more than
    // the digit it saves.
    const int t10 = c.wx->temp_x10;
    char line[64];
    std::snprintf(line, sizeof line, "%dC %s",
                  (t10 + (t10 < 0 ? -5 : 5)) / 10, weather_advice_he(c.wx));

    // STALENESS IS THE COLOUR, NOT A WORD. This used to draw "old" in red at
    // the left end, which is clearer in isolation and cost 74px of the one
    // line the advice has to fit into -- enough to elide "coat and gloves"
    // down to "..." on exactly the morning it matters. The whole line goes red
    // instead: unmistakable on an otherwise black page, and free.
    const uint32_t colour = weather_is_stale(c.wx, c.now_utc) ? TFT_RED : TFT_BLACK;

    // RTL from the right edge of the band: the temperature lands rightmost --
    // first, in Hebrew reading order -- and the advice follows it leftwards.
    he::draw_line_rtl_fit(metrics(), DL_CANVAS_W - DL_MARGIN_X - DL_BAND_PAD,
                          DL_MARGIN_X + DL_BAND_PAD, y, line, colour);
}

}  // namespace

void page_daily_draw(const page_daily_content &c)
{
    const he_metrics_t *m = metrics();

    // What is present decides the layout; the layout decides where it goes.
    daily_flags_t f;
    f.schedule = c.sched && schedule_for_day(c.sched, c.today)[0] != 0;
    f.weather  = c.wx && c.wx->fetched_at != 0;
    f.callout  = c.kids && kids_pick_callout(c.kids, c.today) >= 0;
    f.birthday = c.kids && kids_birthday_on(c.kids, c.month, c.day) >= 0;

    daily_layout_t L;
    daily_layout(&f, &L);

    ui_canvas().startWrite();
    ui_canvas().fillScreen(TFT_WHITE);

    draw_header(c);

    if (L.band_h > 0)
        ui_canvas().drawRect(DL_MARGIN_X, L.band_y,
                            DL_CANVAS_W - 2 * DL_MARGIN_X, L.band_h, TFT_BLACK);

    if (L.schedule_y != DL_ABSENT)
        he::draw_line_rtl_fit(m, DL_CANVAS_W - DL_MARGIN_X - DL_BAND_PAD,
                              DL_MARGIN_X + DL_BAND_PAD,
                              L.schedule_y, schedule_for_day(c.sched, c.today));

    if (L.weather_y != DL_ABSENT) draw_weather(c, L.weather_y);

    if (L.birthday_y != DL_ABSENT) {
        int who = kids_birthday_on(c.kids, c.month, c.day);
        ui_canvas().drawRect(DL_MARGIN_X, L.birthday_y,
                            DL_CANVAS_W - 2 * DL_MARGIN_X,
                            2 * HE_H + 16, TFT_RED);
        // Red, and the only place colour carries meaning rather than decorating.
        he::draw_line_rtl(m, DL_CANVAS_W - DL_MARGIN_X - 12, L.birthday_y + 8,
                          "\xd7\x99\xd7\x95\xd7\x9d \xd7\x94\xd7\x95\xd7\x9c\xd7\x93\xd7\xaa "
                          "\xd7\xa9\xd7\x9e\xd7\x97", TFT_RED);
        if (who >= 0)
            he::draw_line_rtl(m, DL_CANVAS_W - DL_MARGIN_X - 12,
                              L.birthday_y + 8 + HE_H, c.kids->kid[who].name,
                              TFT_RED);
    }

    if (L.callout_y != DL_ABSENT) {
        int who = kids_pick_callout(c.kids, c.today);
        if (who >= 0) {
            char line[KID_NAME_MAX + 24];
            std::snprintf(line, sizeof line, "%s, \xd7\x96\xd7\x90\xd7\xaa "
                          "\xd7\x91\xd7\xa9\xd7\x91\xd7\x99\xd7\x9c\xd7\x9a",
                          c.kids->kid[who].name);
            he::draw_line_rtl_fit(m, DL_CANVAS_W - DL_MARGIN_X, DL_MARGIN_X,
                                  L.callout_y, line);
        }
    }

    // THE RIDDLE BLOCK, CENTRED IN WHAT IS LEFT.
    //
    // It used to start hard against riddle_top and flow down, which meant the
    // composition was decided by how long the riddle happened to be. A short
    // one ("what has teeth and never bites?") left the bottom 45% of the panel
    // blank and the whole page looked top-weighted and unfinished; a long one
    // filled it and looked deliberate. Same code, same day, opposite result.
    //
    // Measuring the block first and centring it costs one extra line-break
    // pass over a string under 200 bytes, and makes both cases look composed.
    const int riddle_w = DL_CANVAS_W - 2 * DL_MARGIN_X;
    const char *q = c.question ? c.question : "";
    const int q_lines = he::wrapped_lines(m, riddle_w, q, 5);
    int block_h = q_lines * (HE_H - 6);
    if (c.show_answer && c.answer)  block_h += 20 + 16 + HE_H * kAnswerScale;
    else if (c.has_choices)         block_h += 12 + 3 * kChoiceH + 2 * kChoiceGap;

    const int slack = DL_BODY_BOTTOM - L.riddle_top - block_h;
    int y = L.riddle_top + (slack > 0 ? slack / 2 : 0);

    y = he::draw_wrapped(m, y, DL_MARGIN_X, DL_CANVAS_W - DL_MARGIN_X,
                         DL_BODY_BOTTOM, q, 5);
    if (y < 0) y = L.riddle_top + 3 * (HE_H - 6);   // clamped; drew what it could

    if (c.show_answer && c.answer) {
        y += 20;
        ui_canvas().drawFastHLine(DL_MARGIN_X, y, DL_CANVAS_W - 2 * DL_MARGIN_X,
                                 TFT_BLACK);
        y += 16;
        // DOUBLE SIZE, AND NOW ACTUALLY. This comment used to promise the
        // answer was "larger than the question was ... readable across a
        // room", and draw_line_rtl had no size parameter at all -- the payoff
        // the whole day builds to was rendered at exactly the weight of a
        // timetable entry. Pixel doubling of a 24x41 blob is coarse up close
        // and unmistakable from the far side of a room, which is the trade
        // this page wants.
        he::draw_line_rtl(m, DL_CANVAS_W - DL_MARGIN_X, y, c.answer, TFT_RED,
                          kAnswerScale);
    } else if (c.has_choices) {
        y += 12;
        for (int i = 0; i < 3; i++) {
            if (y + kChoiceH > DL_BODY_BOTTOM) break;
            // NO A/B/C MARKERS. They were printed top-to-bottom as A, B, C and
            // the case is silkscreened C, B, A for those same three buttons --
            // see wake.hpp, which documents the inversion. A child who trusted
            // the letters pressed the opposite end of the list. The comment
            // here claimed they "match the silkscreen"; two of the three never
            // did.
            //
            // Deleting them is the whole fix. The real affordance was always
            // position -- three lines beside three buttons in the same order,
            // which is correct and was correct before -- and the letters only
            // ever contradicted it. It also takes the last Latin off a Hebrew
            // page and gives each choice back 34px of line.
            he::draw_line_rtl(m, DL_CANVAS_W - DL_MARGIN_X, y,
                              c.choices[i] ? c.choices[i] : "");
            y += kChoiceH + kChoiceGap;
        }
    }

    ui_canvas().endWrite();
}
