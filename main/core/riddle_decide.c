// Morning Riddle: the decision core. See riddle_decide.h for the no-IDF rule.

#include "riddle_decide.h"

#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- schedule ---

// Applying the timezone means mutating a process-global (TZ + tzset()), so it
// is set and restored around each conversion. Without the restore, a caller
// that also uses localtime() elsewhere silently inherits Israel time.
static void tz_push(const char *tz, char *saved, size_t n)
{
    const char *cur = getenv("TZ");
    if (cur) { strncpy(saved, cur, n - 1); saved[n - 1] = '\0'; }
    else     { saved[0] = '\0'; }
    setenv("TZ", tz, 1);
    tzset();
}

static void tz_pop(const char *saved)
{
    if (saved[0]) setenv("TZ", saved, 1);
    else          unsetenv("TZ");
    tzset();
}

// Days from 1970-01-01 for a proleptic Gregorian y/m/d. Hinnant's algorithm.
// Used instead of tm_yday so the day key stays monotonic across a year end.
static int32_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int32_t)(era * 146097 + (int)doe - 719468);
}

int32_t riddle_local_day(time_t utc, const char *tz)
{
    char saved[64];
    struct tm lt;
    tz_push(tz, saved, sizeof saved);
    localtime_r(&utc, &lt);
    tz_pop(saved);
    return days_from_civil(lt.tm_year + 1900, (unsigned)lt.tm_mon + 1,
                           (unsigned)lt.tm_mday);
}

// Local wall-clock h:m on the calendar day of `base`, shifted by `day_offset`,
// resolved to a UTC instant. tm_isdst = -1 asks the C library to work out
// whether that wall time is in DST -- which is the whole reason this is not
// arithmetic on a fixed offset.
static time_t local_hm_to_utc(const struct tm *base, int day_offset,
                              int hour, int min)
{
    struct tm t = *base;
    t.tm_mday += day_offset;
    t.tm_hour  = hour;
    t.tm_min   = min;
    t.tm_sec   = 0;
    t.tm_isdst = -1;
    return mktime(&t);            // normalises the date rollover too
}

time_t riddle_next_wake(time_t utc, const char *tz, int *is_morning)
{
    char saved[64];
    struct tm lt;
    time_t best = (time_t)-1;
    int best_morning = 1;

    tz_push(tz, saved, sizeof saved);
    localtime_r(&utc, &lt);

    // Today's two slots, then tomorrow's morning. First one strictly after
    // `utc` wins. Strictly, so an alarm firing exactly on the second does not
    // re-arm itself for the same instant and spin.
    const int slots[3][3] = {
        { 0, RIDDLE_MORNING_HOUR, RIDDLE_MORNING_MIN },
        { 0, RIDDLE_REVEAL_HOUR,  RIDDLE_REVEAL_MIN  },
        { 1, RIDDLE_MORNING_HOUR, RIDDLE_MORNING_MIN },
    };
    for (int i = 0; i < 3; i++) {
        time_t cand = local_hm_to_utc(&lt, slots[i][0], slots[i][1], slots[i][2]);
        if (cand != (time_t)-1 && cand > utc) {
            best = cand;
            best_morning = (slots[i][1] == RIDDLE_MORNING_HOUR);
            break;
        }
    }
    tz_pop(saved);

    if (is_morning) *is_morning = best_morning;
    return best;
}

// ----------------------------------------------------------- state machine ---

// Starts today's riddle: advance the queue, wrapping at the end so the wall is
// never blank (CEO 7A -- a repeat is a mild disappointment, a blank reads as a
// broken device), and settle the streak before the day is overwritten.
static void begin_day(const riddle_input_t *in, riddle_nvs_t *st)
{
    if (in->batch_n > 0) {
        // First run (RS_IDLE) shows riddle 0 rather than 1.
        uint16_t next = (st->state == RS_IDLE) ? 0
                                               : (uint16_t)((st->idx + 1) % in->batch_n);
        // Prefer an item whose weekend flag matches the day. Bounded by
        // batch_n so a batch that is entirely one kind still lands on
        // something -- a repeat is a mild disappointment, a blank reads as a
        // broken device, and a weekday riddle on a Saturday is neither.
        if (in->weekend) {
            for (uint16_t tries = 0; tries < in->batch_n; tries++) {
                if (in->weekend[next] == in->want_weekend) break;
                next = (uint16_t)((next + 1) % in->batch_n);
            }
        }
        st->idx = next;
    }
    // Participation, not accuracy (CEO 17A): a wrong guess keeps the run
    // alive. Only a day with NO guess at all breaks it -- turning a morning
    // joke into an exam an eight-year-old can fail before school is the
    // fastest way to make the wall something they avoid.
    if (st->last_played_day != in->today - 1) st->streak = 0;

    // A kid's streak breaks when their PREVIOUS TURN went untaken, and their
    // previous turn was kids_n days ago, not yesterday. Checking `today - 1`
    // here would reset every per-kid streak the moment it started.
    if (in->whose_turn >= 0 && in->whose_turn < KIDS_MAX && in->kids_n > 0) {
        const int t = in->whose_turn;
        if (st->kid_last[t] != in->today - (int32_t)in->kids_n)
            st->kid_streak[t] = 0;
    }

    st->issue++;                      // mornings published; never resets
    st->day   = in->today;
    st->state = RS_QUESTION_SHOWN;
    st->guess = RIDDLE_NO_GUESS;
}

riddle_action_e riddle_decide(const riddle_input_t *in, riddle_nvs_t *st)
{
    switch (in->reason) {

    case WAKE_MORNING:
        // Double-fire guard: a second 06:30 wake on the same civil day
        // redraws without consuming another riddle.
        if (st->state != RS_IDLE && st->day == in->today)
            return ACT_SHOW_QUESTION;
        begin_day(in, st);
        return ACT_SHOW_QUESTION;

    case WAKE_AFTERNOON:
        // The morning never happened -- board was off, or out of range. An
        // answer to a question nobody saw is worse than a late riddle.
        if (st->state == RS_IDLE || st->day != in->today) {
            begin_day(in, st);
            return ACT_SHOW_QUESTION;
        }
        if (st->state == RS_ANSWER_SHOWN) return ACT_NONE;   // already revealed
        st->state = RS_ANSWER_SHOWN;
        return ACT_SHOW_ANSWER;

    case WAKE_GUESS:
        // A STALE SCREEN IS THE ONLY REAL REFUSAL. If the panel is showing
        // another day's riddle the board missed a wake, and pressing cannot
        // mean anything -- that is a fault, and it should feel like one.
        if (st->day != in->today) return ACT_NONE;

        // Everything else on today's page is a press the board HEARD. Already
        // answered by a sibling, pressed twice by the same child, or pressed
        // after the 13:00 reveal when there is nothing left to guess: none of
        // those is a mistake, and none of them changes the state.
        //
        // Late guesses are still ignored, which is what closes the 12:59:58
        // race: whichever of the guess and the 13:00 alarm commits first wins,
        // and the loser is a no-op rather than a contradictory redraw. It is
        // now an acknowledged no-op instead of a rejected one.
        if (st->state != RS_QUESTION_SHOWN) return ACT_ACK_ONLY;
        st->state = RS_GUESSED;
        st->guess = in->guess;
        if (st->last_played_day != in->today) {
            st->streak++;                     // once per day, however many presses
            st->last_played_day = in->today;
            // The turn is credited to whoever the page named this morning.
            // The board cannot tell who pressed; the page can tell them whose
            // turn it was, which is the same contract a family already runs on.
            if (in->whose_turn >= 0 && in->whose_turn < KIDS_MAX) {
                st->kid_streak[in->whose_turn]++;
                st->kid_last[in->whose_turn] = in->today;
            }
        }
        return ACT_SHOW_RESULT;

    case WAKE_REVEAL:
        if (st->state == RS_IDLE)         return ACT_NONE;
        if (st->state == RS_ANSWER_SHOWN) return ACT_NONE;
        st->state = RS_ANSWER_SHOWN;
        return ACT_SHOW_ANSWER;

    case WAKE_MENU:
        // Read-only on purpose: opening the tile must not consume a riddle,
        // break a streak, or reveal anything.
        if (st->state == RS_IDLE)         return ACT_SHOW_QUESTION;
        if (st->state == RS_ANSWER_SHOWN) return ACT_SHOW_ANSWER;
        return ACT_SHOW_QUESTION;

    default:
        return ACT_NONE;
    }
}
