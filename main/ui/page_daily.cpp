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

// Markers name the buttons, so they are ASCII and short. A, B, C match the
// silkscreen rather than arrows, which would need a direction convention.
const char *kMarks[3] = { "A", "B", "C" };

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

    // The streak is address, not information -- the page telling its reader it
    // has been paying attention. Below two days there is nothing to say.
    if (c.streak > 1) {
        char s[24];
        std::snprintf(s, sizeof s, "%u days", (unsigned)c.streak);
        ui_canvas().drawString(s, DL_CANVAS_W - DL_MARGIN_X - 90, DL_HDR_Y);
    }

    ui_canvas().drawFastHLine(DL_MARGIN_X, DL_HDR_RULE_Y,
                             DL_CANVAS_W - 2 * DL_MARGIN_X, TFT_BLACK);
}

void draw_weather(const page_daily_content &c, int y)
{
    if (!c.wx) return;
    char line[64];
    int n = std::snprintf(line, sizeof line, "%d.%dC  %s",
                          c.wx->temp_x10 / 10, std::abs(c.wx->temp_x10 % 10),
                          wmo_label(c.wx->wmo));
    if ((c.wx->hi_x10 || c.wx->lo_x10) && n > 0 && n < (int)sizeof line)
        std::snprintf(line + n, sizeof line - n, "   %d/%d",
                      c.wx->hi_x10 / 10, c.wx->lo_x10 / 10);

    ui_canvas().setTextColor(TFT_BLACK);
    ui_canvas().setTextSize(2);
    ui_canvas().drawString(line, DL_MARGIN_X + DL_BAND_PAD, y + 8);

    // Say so when it is old, in red. A confidently wrong temperature is worse
    // than an obviously stale one, and the fetch fails silently by design.
    if (weather_is_stale(c.wx, c.now_utc)) {
        ui_canvas().setTextColor(TFT_RED);
        ui_canvas().drawString("old", DL_CANVAS_W - DL_MARGIN_X - 50, y + 8);
        ui_canvas().setTextColor(TFT_BLACK);
    }
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
        he::draw_line_rtl(m, DL_CANVAS_W - DL_MARGIN_X - DL_BAND_PAD,
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
            he::draw_line_rtl(m, DL_CANVAS_W - DL_MARGIN_X, L.callout_y, line);
        }
    }

    // The riddle. Five lines is the cap the layout budget assumes; a question
    // longer than that is a generator problem, not a rendering one.
    int y = he::draw_wrapped(m, L.riddle_top, DL_MARGIN_X,
                             DL_CANVAS_W - DL_MARGIN_X, DL_BODY_BOTTOM,
                             c.question ? c.question : "", 5);
    if (y < 0) y = L.riddle_top + 3 * (HE_H - 6);   // clamped; drew what it could

    if (c.show_answer && c.answer) {
        y += 20;
        ui_canvas().drawFastHLine(DL_MARGIN_X, y, DL_CANVAS_W - 2 * DL_MARGIN_X,
                                 TFT_BLACK);
        y += 16;
        // In red, and larger than the question was. This is the payoff the
        // whole day builds to, and it should be readable across a room.
        he::draw_line_rtl(m, DL_CANVAS_W - DL_MARGIN_X, y, c.answer, TFT_RED);
    } else if (c.has_choices) {
        y += 12;
        for (int i = 0; i < 3; i++) {
            if (y + kChoiceH > DL_BODY_BOTTOM) break;
            ui_canvas().setTextColor(TFT_BLACK);
            ui_canvas().setTextSize(2);
#if PD_BUTTONS_ON_LEFT_EDGE
            ui_canvas().drawString(kMarks[i], DL_MARGIN_X + 4, y + 12);
            he::draw_line_rtl(m, DL_CANVAS_W - DL_MARGIN_X - 8, y,
                              c.choices[i] ? c.choices[i] : "");
#else
            ui_canvas().drawString(kMarks[i], DL_CANVAS_W - DL_MARGIN_X - 20, y + 12);
            he::draw_line_rtl(m, DL_CANVAS_W - DL_MARGIN_X - 34, y,
                              c.choices[i] ? c.choices[i] : "");
#endif
            y += kChoiceH + kChoiceGap;
        }
    }

    ui_canvas().endWrite();
}
