#include "page_daily.hpp"

#include <M5Unified.h>

#include "gfx_target.hpp"
#include <cstdio>
#include <cstdlib>

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

void draw_header(const page_daily_content &c)
{
    // THE MASTHEAD, ON ONE LINE: nameplate right, dateline left.
    //
    // Stacking them the way a broadsheet does would cost 41px the crowded days
    // do not have. Flanking works because the two are different sizes -- which
    // is the whole reason the small face was cut -- so they read as nameplate
    // and dateline rather than as two competing headings.
    //
    // Drawn at y=0, not DL_HDR_Y, and that is measured rather than fudged: ink
    // occupies rows 5..37 of the 41px body cell, so a cell at y=0 lands its ink
    // at 5..37 and clears the rule at DL_HDR_RULE_Y=42 by five pixels.
    he::draw_line_rtl(he::body(), DL_CANVAS_W - DL_MARGIN_X, 0, MASTHEAD_NAME);

    // "יום שני · 31.08 · גיליון 12"
    //
    // A REAL ISSUE NUMBER, WHICH IS NOT THE STREAK. This printed the household
    // streak for one build, and a paper's issue number does not reset because
    // nobody read yesterday's. riddle_nvs_t counts mornings published now and
    // this prints that. It is also no longer a scoreboard, which matters: a
    // scoreboard in the masthead invites a child to feel behind before they
    // have read a word.
    //
    // Below two it says nothing: "issue 1" is not a boast, it is an admission.
    char dl[64];
    int n = std::snprintf(dl, sizeof dl, "%s \xc2\xb7 %s",
                          schedule_weekday_he(schedule_weekday(c.today)),
                          c.date ? c.date : "");
    if (c.issue > 1 && n > 0 && n < (int)sizeof dl)
        std::snprintf(dl + n, sizeof dl - n,
                      " \xc2\xb7 \xd7\x92\xd7\x99\xd7\x9c\xd7\x99\xd7\x95\xd7\x9f %u",  // "issue N"
                      (unsigned)c.issue);
    // Left-flag position, so it sits opposite the nameplate rather than under
    // it. Measured, not guessed, because the day name and the issue number both
    // change width.
    he::draw_line_rtl(he::small(),
                      DL_MARGIN_X + he::measure(he::small(), dl), 9, dl);

    ui_canvas().fillRect(DL_MARGIN_X, DL_HDR_RULE_Y,
                         DL_CANVAS_W - 2 * DL_MARGIN_X, DL_HDR_RULE_H,
                         TFT_BLACK);
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

    // RTL from the right edge: the temperature lands rightmost -- first, in
    // Hebrew reading order -- and the advice follows it leftwards.
    he::draw_line_rtl_fit(he::body(), DL_CANVAS_W - DL_MARGIN_X, DL_MARGIN_X,
                          y, line, colour);
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
    f.image_h  = c.image ? (int)c.image->h : 0;

    daily_layout_t L;
    daily_layout(&f, &L);

    ui_canvas().startWrite();
    ui_canvas().fillScreen(TFT_WHITE);

    draw_header(c);

    // TODAY'S PICTURE, if the layout could afford one. Six colours, which is
    // the whole gamut and the first time this page has used more than two.
    //
    // Drawn a pixel at a time into the RAM canvas. That is 36,000 drawPixel
    // calls for a 400x90 strip, which sounds careless and costs single-digit
    // milliseconds against a panel refresh of seventeen seconds; a row-packing
    // optimisation here would be measuring the wrong thing.
    if (L.image_h > 0 && c.image) {
        static const uint32_t kInk[STRIP_INK_COUNT] = {
            TFT_BLACK, TFT_WHITE, TFT_RED, TFT_YELLOW, TFT_BLUE, TFT_GREEN,
        };
        for (int row = 0; row < L.image_h; row++) {
            for (int x = 0; x < STRIP_W; x++) {
                const uint8_t ink = strip_at(c.image, x, row);
                // White is the page. Skipping it is not an optimisation, it is
                // what lets an illustration sit on the paper rather than in a
                // box -- the canvas is already white.
                if (ink == STRIP_WHITE) continue;
                ui_canvas().drawPixel(x, L.image_y + row, kInk[ink]);
            }
        }
    }

    // RULES, NOT A BOX. A bordered rectangle around the timetable and the
    // weather made them look like a form to be filled in; two hairlines make
    // them look like a column to be read. Same pixels, opposite reading.
    if (L.band_h > 0) {
        const int w = DL_CANVAS_W - 2 * DL_MARGIN_X;
        ui_canvas().fillRect(DL_MARGIN_X, L.band_y, w, DL_HAIR, TFT_BLACK);
        ui_canvas().fillRect(DL_MARGIN_X, L.band_y + L.band_h - DL_HAIR, w,
                             DL_HAIR, TFT_BLACK);
    }

    // Full margin-to-margin now: with no box there is no border to sit inside,
    // and the timetable gets DL_BAND_PAD*2 more width -- which is exactly the
    // line that has been eliding subjects.
    if (L.schedule_y != DL_ABSENT)
        he::draw_line_rtl_fit(he::body(), DL_CANVAS_W - DL_MARGIN_X,
                              DL_MARGIN_X, L.schedule_y,
                              schedule_for_day(c.sched, c.today));

    if (L.weather_y != DL_ABSENT) draw_weather(c, L.weather_y);

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

    // A KICKER, BUT ONLY WHEN THE PAGE WOULD OTHERWISE LIE. The paper is called
    // "the morning riddle", so a riddle needs no label -- but a joke read as a
    // riddle is a puzzle with no solution, and a child who cannot solve it
    // concludes they are bad at this rather than that it was a joke.
    const char *kicker = kind_label(c.kind);
    const int kicker_h = kicker ? DL_SMALL_H + 6 : 0;

    const char *why = (c.why && c.why[0]) ? c.why : nullptr;
    const int why_lines = (c.show_answer && why)
                              ? he::wrapped_lines(m, riddle_w, why, 3) : 0;

    int block_h = kicker_h + q_lines * (HE_H - 6);
    if (c.show_answer && c.answer) {
        block_h += 20 + 16 + HE_H * kAnswerScale;
        if (why_lines) block_h += 18 + why_lines * (HE_H - 6);
    } else if (c.has_choices) {
        block_h += 12 + 3 * kChoiceH + 2 * kChoiceGap;
    }

    const int slack = DL_BODY_BOTTOM - L.riddle_top - block_h;
    int y = L.riddle_top + (slack > 0 ? slack / 2 : 0);

    if (kicker) {
        he::draw_line_rtl(he::small(), DL_CANVAS_W - DL_MARGIN_X, y, kicker,
                          TFT_RED);
        y += kicker_h;
    }

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
        he::draw_line_rtl(he::body(), DL_CANVAS_W - DL_MARGIN_X, y, c.answer,
                          TFT_RED, kAnswerScale);
        y += HE_H * kAnswerScale;

        // AND WHY IT IS THE ANSWER. In black under the red, at reading size:
        // the answer is the payoff, this is the part that makes the payoff
        // worth having. Optional -- an item with no `why` draws exactly the
        // page it drew before this existed.
        if (why_lines) {
            y += 18;
            he::draw_wrapped(m, y, DL_MARGIN_X, DL_CANVAS_W - DL_MARGIN_X,
                             DL_BODY_BOTTOM, why, 3);
        }
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
            he::draw_line_rtl(he::body(), DL_CANVAS_W - DL_MARGIN_X, y,
                              c.choices[i] ? c.choices[i] : "");
            y += kChoiceH + kChoiceGap;
        }
    }

    ui_canvas().endWrite();
}
