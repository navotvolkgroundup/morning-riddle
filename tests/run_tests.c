// Host tests for the Morning Riddle decision core.
//
//   cc -std=c99 -Wall -Wextra -I main/page_riddle \
//      tests/run_tests.c main/page_riddle/riddle_decide.c -o /tmp/rt && /tmp/rt
//
// or just: make test
//
// No framework and no fixtures on purpose. The value here is that these run in
// milliseconds on a Mac, against a board that has been reachable for about 11
// minutes of the last three hours. The DST block below is the reason this file
// exists: that bug is invisible until 2026-10-25 and then the riddle silently
// arrives an hour after the kids have left.

#include "riddle_decide.h"
#include "wake_log.h"
#include "kids.h"
#include "masthead.h"
#include "strip.h"
#include "weather.h"
#include "schedule.h"
#include "sd_json.h"
#include "daily_layout.h"
#include "he_text.h"
#include "riddle_batch.h"
#include "formdata.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    return 1; } } while (0)

// A UTC instant, spelled out, so no epoch magic numbers appear below.
static time_t utc_at(int y, int mo, int d, int h, int mi)
{
    struct tm t;
    memset(&t, 0, sizeof t);
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
    t.tm_hour = h; t.tm_min = mi;
    return timegm(&t);
}

// ------------------------------------------------------------------- DST ---
static int test_dst(void)
{
    int morning = -1;

    // Friday 2026-10-23, still IDT (UTC+3). 00:00Z is 03:00 local, before the
    // 06:30 slot, so the next wake is that morning: 06:30 IDT == 03:30Z.
    time_t got = riddle_next_wake(utc_at(2026, 10, 23, 0, 0), RIDDLE_TZ, &morning);
    CHECK(got == utc_at(2026, 10, 23, 3, 30));
    CHECK(morning == 1);

    // Monday 2026-10-26, IDT has ended (M10.5.0 -> last Sunday, the 25th), so
    // Israel is back on UTC+2. The SAME 06:30 wall time is now 04:30Z.
    got = riddle_next_wake(utc_at(2026, 10, 26, 0, 0), RIDDLE_TZ, &morning);
    CHECK(got == utc_at(2026, 10, 26, 4, 30));
    CHECK(morning == 1);

    // The point, stated as an assertion: a schedule pinned to a fixed UTC
    // offset would fire at the same instant on both days. It must not.
    CHECK(utc_at(2026, 10, 23, 3, 30) != utc_at(2026, 10, 26, 3, 30));

    // Spring forward, for symmetry: M3.4.4/26 puts the change at 02:00 on the
    // Friday after the 4th Thursday of March 2026 (the 27th).
    got = riddle_next_wake(utc_at(2026, 3, 26, 0, 0), RIDDLE_TZ, &morning);
    CHECK(got == utc_at(2026, 3, 26, 4, 30));      // still IST, UTC+2
    got = riddle_next_wake(utc_at(2026, 3, 30, 0, 0), RIDDLE_TZ, &morning);
    CHECK(got == utc_at(2026, 3, 30, 3, 30));      // IDT, UTC+3
    return 0;
}

// -------------------------------------------------------------- schedule ---
static int test_schedule(void)
{
    int morning = -1;

    // Mid-summer (IDT, UTC+3): 05:00Z is 08:00 local, past 06:30, so 13:00.
    time_t got = riddle_next_wake(utc_at(2026, 7, 1, 5, 0), RIDDLE_TZ, &morning);
    CHECK(got == utc_at(2026, 7, 1, 10, 0));       // 13:00 IDT
    CHECK(morning == 0);

    // After the afternoon slot, roll to tomorrow morning.
    got = riddle_next_wake(utc_at(2026, 7, 1, 14, 0), RIDDLE_TZ, &morning);
    CHECK(got == utc_at(2026, 7, 2, 3, 30));
    CHECK(morning == 1);

    // Exactly on the morning instant: strictly-after, so we get 13:00, not the
    // same second again. An alarm that re-arms for now would spin.
    got = riddle_next_wake(utc_at(2026, 7, 1, 3, 30), RIDDLE_TZ, &morning);
    CHECK(got == utc_at(2026, 7, 1, 10, 0));

    // Year boundary: the day key must keep increasing across it.
    int32_t d31 = riddle_local_day(utc_at(2026, 12, 31, 12, 0), RIDDLE_TZ);
    int32_t d01 = riddle_local_day(utc_at(2027, 1, 1, 12, 0), RIDDLE_TZ);
    CHECK(d01 == d31 + 1);

    // 23:00Z in winter is already the next local day (UTC+2).
    int32_t a = riddle_local_day(utc_at(2026, 12, 1, 21, 0), RIDDLE_TZ);
    int32_t b = riddle_local_day(utc_at(2026, 12, 1, 23, 0), RIDDLE_TZ);
    CHECK(b == a + 1);

    // TZ is a process global; the core must put it back as it found it.
    setenv("TZ", "UTC", 1); tzset();
    (void)riddle_next_wake(utc_at(2026, 7, 1, 5, 0), RIDDLE_TZ, NULL);
    CHECK(strcmp(getenv("TZ"), "UTC") == 0);
    return 0;
}

// --------------------------------------------------------- state machine ---
// Zeroed, then filled: riddle_input_t has grown fields (the rotation, the
// weekend flags) that every existing test must leave neutral rather than
// uninitialised, or the state machine reads stack garbage as "kid 87's turn".
static riddle_input_t IN(int reason, int32_t today, int guess, uint16_t n)
{
    riddle_input_t in;
    memset(&in, 0, sizeof in);
    in.reason = (uint8_t)reason; in.today = today;
    in.guess = (int8_t)guess;    in.batch_n = n;
    in.whose_turn = -1;          // no kids configured: no per-kid streak kept
    return in;
}

static int test_state(void)
{
    riddle_nvs_t st;
    memset(&st, 0, sizeof st);
    st.state = RS_IDLE; st.guess = RIDDLE_NO_GUESS;

    // First morning shows riddle 0, not 1.
    riddle_input_t first = IN(WAKE_MORNING, 100, RIDDLE_NO_GUESS, 30);
    CHECK(riddle_decide(&first, &st) == ACT_SHOW_QUESTION);
    CHECK(st.idx == 0 && st.state == RS_QUESTION_SHOWN && st.day == 100);

    // A second 06:30 on the same day redraws without consuming a riddle.
    riddle_input_t again = IN(WAKE_MORNING, 100, RIDDLE_NO_GUESS, 30);
    CHECK(riddle_decide(&again, &st) == ACT_SHOW_QUESTION);
    CHECK(st.idx == 0);

    // A guess: records, gives feedback, and bumps the streak once.
    riddle_input_t g = IN(WAKE_GUESS, 100, 2, 30);
    CHECK(riddle_decide(&g, &st) == ACT_SHOW_RESULT);
    CHECK(st.state == RS_GUESSED && st.guess == 2 && st.streak == 1);

    // Extra presses on today's page are ACKNOWLEDGED, not refused -- one
    // physical press emits three button codes (the bug that shipped in
    // page_news), and, since the rotation, three children who are not named
    // today will press a board that used to buzz at them.
    CHECK(riddle_decide(&g, &st) == ACT_ACK_ONLY);
    CHECK(st.streak == 1);
    CHECK(st.state == RS_GUESSED && st.guess == 2);   // and it changes nothing

    // 13:00 reveals, knowing a guess was made.
    riddle_input_t pm = IN(WAKE_AFTERNOON, 100, RIDDLE_NO_GUESS, 30);
    CHECK(riddle_decide(&pm, &st) == ACT_SHOW_ANSWER);
    CHECK(st.state == RS_ANSWER_SHOWN);
    CHECK(riddle_decide(&pm, &st) == ACT_NONE);        // idempotent

    // A press arriving after the reveal changes nothing, and is acknowledged
    // rather than refused: there is nothing left to guess, but the child did
    // not do anything wrong.
    CHECK(riddle_decide(&g, &st) == ACT_ACK_ONLY);
    CHECK(st.guess == 2);

    // Next day: advances, and the streak survives because yesterday counted.
    riddle_input_t d2 = IN(WAKE_MORNING, 101, RIDDLE_NO_GUESS, 30);
    CHECK(riddle_decide(&d2, &st) == ACT_SHOW_QUESTION);
    CHECK(st.idx == 1 && st.streak == 1 && st.guess == RIDDLE_NO_GUESS);

    // Skip a day without guessing -> streak resets.
    riddle_input_t d4 = IN(WAKE_MORNING, 103, RIDDLE_NO_GUESS, 30);
    CHECK(riddle_decide(&d4, &st) == ACT_SHOW_QUESTION);
    CHECK(st.streak == 0 && st.idx == 2);
    return 0;
}

static int test_state_edges(void)
{
    riddle_nvs_t st;

    // Participation, not accuracy: a WRONG guess still keeps the run alive.
    memset(&st, 0, sizeof st);
    st.state = RS_QUESTION_SHOWN; st.day = 200; st.last_played_day = 199;
    st.streak = 5; st.guess = RIDDLE_NO_GUESS;
    riddle_input_t wrong = IN(WAKE_GUESS, 200, 0, 30);   // whatever the answer is
    CHECK(riddle_decide(&wrong, &st) == ACT_SHOW_RESULT);
    CHECK(st.streak == 6);

    // Queue wraps at the end rather than going blank (CEO 7A).
    memset(&st, 0, sizeof st);
    st.state = RS_QUESTION_SHOWN; st.idx = 29; st.day = 300;
    riddle_input_t nxt = IN(WAKE_MORNING, 301, RIDDLE_NO_GUESS, 30);
    CHECK(riddle_decide(&nxt, &st) == ACT_SHOW_QUESTION);
    CHECK(st.idx == 0);

    // An empty batch must not divide by zero.
    memset(&st, 0, sizeof st);
    st.state = RS_QUESTION_SHOWN; st.idx = 3; st.day = 300;
    riddle_input_t empty = IN(WAKE_MORNING, 301, RIDDLE_NO_GUESS, 0);
    CHECK(riddle_decide(&empty, &st) == ACT_SHOW_QUESTION);
    CHECK(st.idx == 3);

    // 13:00 with no morning at all (board was off): show the riddle, not an
    // orphan answer to a question nobody saw.
    memset(&st, 0, sizeof st);
    st.state = RS_IDLE; st.guess = RIDDLE_NO_GUESS;
    riddle_input_t orphan = IN(WAKE_AFTERNOON, 400, RIDDLE_NO_GUESS, 30);
    CHECK(riddle_decide(&orphan, &st) == ACT_SHOW_QUESTION);
    CHECK(st.state == RS_QUESTION_SHOWN && st.day == 400);

    // Yesterday's screen still up when the afternoon fires: same rule.
    memset(&st, 0, sizeof st);
    st.state = RS_ANSWER_SHOWN; st.day = 399; st.idx = 4;
    riddle_input_t stale = IN(WAKE_AFTERNOON, 400, RIDDLE_NO_GUESS, 30);
    CHECK(riddle_decide(&stale, &st) == ACT_SHOW_QUESTION);
    CHECK(st.idx == 5 && st.day == 400);

    // Reveal-early from the question, then again -> no-op.
    memset(&st, 0, sizeof st);
    st.state = RS_QUESTION_SHOWN; st.day = 500;
    riddle_input_t rv = IN(WAKE_REVEAL, 500, RIDDLE_NO_GUESS, 30);
    CHECK(riddle_decide(&rv, &st) == ACT_SHOW_ANSWER);
    CHECK(riddle_decide(&rv, &st) == ACT_NONE);

    // The menu is read-only: no advance, no reveal, no streak change.
    memset(&st, 0, sizeof st);
    st.state = RS_QUESTION_SHOWN; st.idx = 7; st.day = 600;
    st.streak = 3; st.last_played_day = 599;
    riddle_nvs_t before = st;
    riddle_input_t menu = IN(WAKE_MENU, 601, RIDDLE_NO_GUESS, 30);
    CHECK(riddle_decide(&menu, &st) == ACT_SHOW_QUESTION);
    CHECK(memcmp(&before, &st, sizeof st) == 0);

    // A guess against yesterday's screen is refused.
    memset(&st, 0, sizeof st);
    st.state = RS_QUESTION_SHOWN; st.day = 700; st.streak = 2;
    riddle_input_t late = IN(WAKE_GUESS, 701, 1, 30);
    CHECK(riddle_decide(&late, &st) == ACT_NONE);
    CHECK(st.streak == 2);

    // CONTRACT, load-bearing: WAKE_MENU does NOT advance a stale day. It is
    // deliberately read-only, so on a day the 06:30 wake never ran it shows
    // yesterday's riddle and then refuses guesses against it. Callers must
    // detect the stale day themselves and pass WAKE_MORNING instead -- which
    // is what page_riddle_show() does, and it matters because ambient mode
    // ships defaulting OFF, making the menu tile the only way in.
    // Do not "fix" this by auto-advancing here; that would let opening the
    // tile consume a riddle and silently break the ritual.
    memset(&st, 0, sizeof st);
    st.state = RS_QUESTION_SHOWN; st.day = 800; st.idx = 9;
    riddle_input_t stale_menu = IN(WAKE_MENU, 801, RIDDLE_NO_GUESS, 30);
    CHECK(riddle_decide(&stale_menu, &st) == ACT_SHOW_QUESTION);
    CHECK(st.day == 800 && st.idx == 9);          // untouched, on purpose
    riddle_input_t refused = IN(WAKE_GUESS, 801, 1, 30);
    CHECK(riddle_decide(&refused, &st) == ACT_NONE);
    return 0;
}

// -------------------------------------------------------------- wake ring ---
static wake_rec_t REC(uint32_t when, uint8_t outcome, uint8_t flags)
{
    wake_rec_t r;
    memset(&r, 0, sizeof r);
    r.when = when; r.outcome = outcome; r.flags = flags; r.battery = -1;
    return r;
}

static int test_siblings(void)
{
    // FOUR CHILDREN, ONE PRESS A DAY. The page names one of them; the other
    // three will press it anyway. This is the whole cost of the rotation, and
    // the assertion set that keeps it from being a telling-off.
    riddle_nvs_t st;
    memset(&st, 0, sizeof st);
    st.state = RS_IDLE; st.guess = RIDDLE_NO_GUESS;

    riddle_input_t m = IN(WAKE_MORNING, 400, RIDDLE_NO_GUESS, 30);
    m.whose_turn = 1; m.kids_n = 4;
    CHECK(riddle_decide(&m, &st) == ACT_SHOW_QUESTION);

    // First press of the day counts, whoever physically made it. The board
    // cannot tell who pressed; the page told the house whose turn it was.
    riddle_input_t g1 = IN(WAKE_GUESS, 400, 0, 30);
    g1.whose_turn = 1; g1.kids_n = 4;
    CHECK(riddle_decide(&g1, &st) == ACT_SHOW_RESULT);
    CHECK(st.kid_streak[1] == 1 && st.guess == 0);

    // Every press after it today is heard and changes nothing -- not the
    // recorded guess, not the streak, not the state.
    for (int i = 0; i < 5; i++) {
        riddle_input_t again = IN(WAKE_GUESS, 400, 2, 30);
        again.whose_turn = 1; again.kids_n = 4;
        CHECK(riddle_decide(&again, &st) == ACT_ACK_ONLY);
    }
    CHECK(st.guess == 0);                 // the first press still owns the day
    CHECK(st.kid_streak[1] == 1);         // and nobody else's turn was credited
    for (int i = 0; i < KIDS_MAX; i++)
        if (i != 1) CHECK(st.kid_streak[i] == 0);
    CHECK(st.streak == 1);

    // After the reveal, still acknowledged rather than refused.
    riddle_input_t pm = IN(WAKE_AFTERNOON, 400, RIDDLE_NO_GUESS, 30);
    pm.whose_turn = 1; pm.kids_n = 4;
    CHECK(riddle_decide(&pm, &st) == ACT_SHOW_ANSWER);
    riddle_input_t after = IN(WAKE_GUESS, 400, 1, 30);
    after.whose_turn = 1; after.kids_n = 4;
    CHECK(riddle_decide(&after, &st) == ACT_ACK_ONLY);

    // A STALE SCREEN IS STILL A REFUSAL, and must stay one. It means the board
    // missed a wake, which is a fault, and the one case where the buzz is the
    // honest answer. If this ever becomes an ack, a dead board feels identical
    // to a working one.
    riddle_input_t stale = IN(WAKE_GUESS, 401, 1, 30);
    stale.whose_turn = 2; stale.kids_n = 4;
    CHECK(riddle_decide(&stale, &st) == ACT_NONE);

    // So is a press before anything has ever been shown.
    riddle_nvs_t fresh;
    memset(&fresh, 0, sizeof fresh);
    fresh.state = RS_IDLE; fresh.guess = RIDDLE_NO_GUESS;
    riddle_input_t cold = IN(WAKE_GUESS, 400, 0, 30);
    cold.whose_turn = 0; cold.kids_n = 4;
    CHECK(riddle_decide(&cold, &fresh) == ACT_NONE);
    return 0;
}

static int test_issue_and_turns(void)
{
    riddle_nvs_t st;
    memset(&st, 0, sizeof st);
    st.state = RS_IDLE; st.guess = RIDDLE_NO_GUESS;

    // THE ISSUE NUMBER ONLY EVER GOES UP. It printed the household streak for
    // one build, and a paper's issue number does not reset because nobody read
    // yesterday's. Three mornings with no guesses at all: streak stays 0, issue
    // reaches 3.
    for (int32_t d = 100; d < 103; d++) {
        riddle_input_t in = IN(WAKE_MORNING, d, RIDDLE_NO_GUESS, 30);
        CHECK(riddle_decide(&in, &st) == ACT_SHOW_QUESTION);
    }
    CHECK(st.issue == 3);
    CHECK(st.streak == 0);

    // A second wake on the same day must not print a second issue.
    riddle_input_t twice = IN(WAKE_MORNING, 102, RIDDLE_NO_GUESS, 30);
    CHECK(riddle_decide(&twice, &st) == ACT_SHOW_QUESTION);
    CHECK(st.issue == 3);

    // ---- per-kid streaks, counted in TURNS ---------------------------------
    memset(&st, 0, sizeof st);
    st.state = RS_IDLE; st.guess = RIDDLE_NO_GUESS;

    // Two kids, so kid 0's turns are the even days. Play four of them in a row.
    for (int32_t d = 200; d <= 206; d += 2) {
        riddle_input_t m = IN(WAKE_MORNING, d, RIDDLE_NO_GUESS, 30);
        m.whose_turn = 0; m.kids_n = 2;
        CHECK(riddle_decide(&m, &st) == ACT_SHOW_QUESTION);
        riddle_input_t g = IN(WAKE_GUESS, d, 1, 30);
        g.whose_turn = 0; g.kids_n = 2;
        CHECK(riddle_decide(&g, &st) == ACT_SHOW_RESULT);
    }
    CHECK(st.kid_streak[0] == 4);
    CHECK(st.kid_streak[1] == 0);            // never had a turn taken

    // COUNTED IN TURNS, NOT DAYS, and this is the assertion that catches it:
    // kid 0's turns are two days apart, so a rule that checked `today - 1`
    // would have reset this streak on every single one of those mornings.
    CHECK(st.kid_last[0] == 206);

    // Skip kid 0's next turn (208), then play the one after (210). The run
    // breaks and restarts at 1.
    riddle_input_t skip = IN(WAKE_MORNING, 208, RIDDLE_NO_GUESS, 30);
    skip.whose_turn = 0; skip.kids_n = 2;
    riddle_decide(&skip, &st);
    CHECK(st.kid_streak[0] == 4);            // not yet reset: 206 was the last turn
    riddle_input_t after = IN(WAKE_MORNING, 210, RIDDLE_NO_GUESS, 30);
    after.whose_turn = 0; after.kids_n = 2;
    riddle_decide(&after, &st);
    CHECK(st.kid_streak[0] == 0);            // 208 went untaken; the run is over
    riddle_input_t g2 = IN(WAKE_GUESS, 210, 0, 30);
    g2.whose_turn = 0; g2.kids_n = 2;
    CHECK(riddle_decide(&g2, &st) == ACT_SHOW_RESULT);
    CHECK(st.kid_streak[0] == 1);

    // No kids configured is the normal unconfigured state. whose_turn is -1
    // and nothing may index off the front of the arrays.
    memset(&st, 0, sizeof st);
    st.state = RS_IDLE; st.guess = RIDDLE_NO_GUESS;
    riddle_input_t nokids = IN(WAKE_MORNING, 300, RIDDLE_NO_GUESS, 30);
    CHECK(riddle_decide(&nokids, &st) == ACT_SHOW_QUESTION);
    riddle_input_t ng = IN(WAKE_GUESS, 300, 0, 30);
    CHECK(riddle_decide(&ng, &st) == ACT_SHOW_RESULT);
    for (int i = 0; i < KIDS_MAX; i++) CHECK(st.kid_streak[i] == 0);
    CHECK(st.streak == 1);                   // the household streak still runs
    return 0;
}

static int test_weekend_selection(void)
{
    // Six items: even indices are weekday, odd are weekend.
    bool wk[6];
    for (int i = 0; i < 6; i++) wk[i] = (i % 2) != 0;

    riddle_nvs_t st;
    memset(&st, 0, sizeof st);
    st.state = RS_IDLE; st.guess = RIDDLE_NO_GUESS;

    // A weekday morning lands on a weekday item.
    riddle_input_t d1 = IN(WAKE_MORNING, 100, RIDDLE_NO_GUESS, 6);
    d1.weekend = wk; d1.want_weekend = false;
    CHECK(riddle_decide(&d1, &st) == ACT_SHOW_QUESTION);
    CHECK(wk[st.idx] == false);

    // Saturday asks for a weekend item and gets one, without rewinding.
    riddle_input_t d2 = IN(WAKE_MORNING, 101, RIDDLE_NO_GUESS, 6);
    d2.weekend = wk; d2.want_weekend = true;
    CHECK(riddle_decide(&d2, &st) == ACT_SHOW_QUESTION);
    CHECK(wk[st.idx] == true);

    // A BATCH WITH NOTHING MATCHING MUST NOT SPIN OR BLANK THE WALL. All six
    // weekday, asked for a weekend one: it lands on something, because a
    // weekday riddle on a Saturday is a mild disappointment and a blank panel
    // reads as a broken device.
    bool all_weekday[6] = {false, false, false, false, false, false};
    riddle_input_t d3 = IN(WAKE_MORNING, 102, RIDDLE_NO_GUESS, 6);
    d3.weekend = all_weekday; d3.want_weekend = true;
    CHECK(riddle_decide(&d3, &st) == ACT_SHOW_QUESTION);
    CHECK(st.idx < 6);

    // No flags at all behaves exactly as it did before weekends existed.
    memset(&st, 0, sizeof st);
    st.state = RS_IDLE; st.guess = RIDDLE_NO_GUESS;
    riddle_input_t plain = IN(WAKE_MORNING, 100, RIDDLE_NO_GUESS, 6);
    CHECK(riddle_decide(&plain, &st) == ACT_SHOW_QUESTION);
    CHECK(st.idx == 0);
    riddle_input_t plain2 = IN(WAKE_MORNING, 101, RIDDLE_NO_GUESS, 6);
    CHECK(riddle_decide(&plain2, &st) == ACT_SHOW_QUESTION);
    CHECK(st.idx == 1);
    return 0;
}

static int test_wake_ring(void)
{
    wake_ring_t ring;
    wake_rec_t out[WAKE_LOG_N];
    memset(&ring, 0, sizeof ring);

    CHECK(wake_ring_read(&ring, out, WAKE_LOG_N) == 0);      // empty

    // Newest first, which is the property the screen depends on.
    for (uint32_t i = 1; i <= 3; i++) {
        wake_rec_t r = REC(i * 1000, WO_OK, 0);
        wake_ring_push(&ring, &r);
    }
    CHECK(wake_ring_read(&ring, out, WAKE_LOG_N) == 3);
    CHECK(out[0].when == 3000 && out[1].when == 2000 && out[2].when == 1000);

    // Overfill by five: count saturates and the oldest five are gone.
    memset(&ring, 0, sizeof ring);
    for (uint32_t i = 1; i <= WAKE_LOG_N + 5; i++) {
        wake_rec_t r = REC(i, WO_OK, 0);
        wake_ring_push(&ring, &r);
    }
    int n = wake_ring_read(&ring, out, WAKE_LOG_N);
    CHECK(n == WAKE_LOG_N);
    CHECK(out[0].when == (uint32_t)(WAKE_LOG_N + 5));        // newest
    CHECK(out[n - 1].when == 6);                             // oldest surviving
    for (int i = 1; i < n; i++) CHECK(out[i].when < out[i - 1].when);

    // A short read must still give the newest, not the oldest.
    CHECK(wake_ring_read(&ring, out, 3) == 3);
    CHECK(out[0].when == (uint32_t)(WAKE_LOG_N + 5));

    // A corrupt blob resets rather than indexing off the end.
    memset(&ring, 0, sizeof ring);
    ring.head = 200; ring.count = 200;
    CHECK(!wake_ring_valid(&ring));
    CHECK(wake_ring_read(&ring, out, WAKE_LOG_N) == 0);
    wake_rec_t r = REC(42, WO_OK, 0);
    wake_ring_push(&ring, &r);                               // must not crash
    CHECK(wake_ring_valid(&ring));
    CHECK(wake_ring_read(&ring, out, WAKE_LOG_N) == 1 && out[0].when == 42);
    return 0;
}

static int test_wake_participation(void)
{
    wake_ring_t ring;
    memset(&ring, 0, sizeof ring);
    const uint32_t DAY = 86400;

    // Three consecutive days, guessed on each. Two wakes per day, as the real
    // device produces, so the counter must not double-count a day.
    for (uint32_t d = 10; d <= 12; d++) {
        wake_rec_t am = REC(d * DAY + 6 * 3600, WO_OK, WF_GUESSED);
        wake_rec_t pm = REC(d * DAY + 16 * 3600, WO_OK, WF_GUESSED);
        wake_ring_push(&ring, &am);
        wake_ring_push(&ring, &pm);
    }
    CHECK(wake_ring_recent_guesses(&ring) == 3);

    // A day with no guess ends the run, counted from the newest end.
    memset(&ring, 0, sizeof ring);
    wake_rec_t d10 = REC(10 * DAY, WO_OK, WF_GUESSED);
    wake_rec_t d11 = REC(11 * DAY, WO_OK, 0);            // nobody played
    wake_rec_t d12 = REC(12 * DAY, WO_OK, WF_GUESSED);
    wake_ring_push(&ring, &d10);
    wake_ring_push(&ring, &d11);
    wake_ring_push(&ring, &d12);
    CHECK(wake_ring_recent_guesses(&ring) == 1);

    // The alarm-unset outcome has to be nameable; it is the one failure that
    // is otherwise invisible, so a wrong label defeats the whole screen.
    CHECK(strcmp(wake_outcome_name(WO_ALARM_UNVERIFIED), "ALARM UNSET") == 0);
    CHECK(strcmp(wake_outcome_name(WO_OK), "ok") == 0);
    CHECK(strcmp(wake_outcome_name(200), "?") == 0);
    return 0;
}

// ------------------------------------------------------------------- kids ---
static kids_t KIDS(int n)
{
    kids_t k;
    memset(&k, 0, sizeof k);
    k.count = (uint8_t)n;
    // Deliberately synthetic placeholders, not names. These tests care about
    // selection, not rendering, and this file is in a PUBLIC fork -- real
    // given names here would read as a hint about the actual kids even though
    // the data itself never leaves device NVS.
    const char *names[] = { "AaaA", "BbbB", "CccC", "DddD" };
    const uint8_t mon[]  = { 3, 11, 0, 7 };
    const uint8_t day[]  = { 14, 2, 0, 30 };
    for (int i = 0; i < n && i < KIDS_MAX; i++) {
        snprintf(k.kid[i].name, KID_NAME_MAX, "%s", names[i]);
        k.kid[i].birth_month = mon[i];
        k.kid[i].birth_day   = day[i];
    }
    return k;
}

static int test_kids_empty(void)
{
    // THE load-bearing case: with no data, both features must simply sleep.
    // This is what makes 16A work -- the build ships complete and the kids'
    // details become a config edit rather than a reflash.
    kids_t k;
    memset(&k, 0, sizeof k);
    CHECK(kids_valid(&k));                       // empty is legal, not corrupt
    CHECK(kids_birthday_on(&k, 3, 14) == -1);
    for (int32_t d = 0; d < 100; d++) CHECK(kids_turn_today(&k, d) == -1);
    CHECK(kids_birthday_on(NULL, 3, 14) == -1);
    CHECK(kids_turn_today(NULL, 5) == -1);
    return 0;
}

static int test_kids_birthday(void)
{
    kids_t k = KIDS(4);
    CHECK(kids_birthday_on(&k, 3, 14) == 0);
    CHECK(kids_birthday_on(&k, 11, 2) == 1);
    CHECK(kids_birthday_on(&k, 7, 30) == 3);
    CHECK(kids_birthday_on(&k, 3, 15) == -1);    // one day off
    CHECK(kids_birthday_on(&k, 14, 3) == -1);    // month/day transposed

    // A half-filled record (name, no date) never matches a birthday, but must
    // not disqualify the whole blob -- the name is still good for callouts.
    CHECK(k.kid[2].birth_month == 0);
    CHECK(kids_valid(&k));
    CHECK(kids_birthday_on(&k, 0, 0) == -1);

    // Out-of-range input is rejected rather than matched against zeros.
    CHECK(kids_birthday_on(&k, 13, 1) == -1);
    CHECK(kids_birthday_on(&k, 2, 32) == -1);

    // A corrupt blob is treated as absent.
    kids_t bad = KIDS(2);
    bad.count = KIDS_MAX + 3;
    CHECK(!kids_valid(&bad));
    CHECK(kids_birthday_on(&bad, 3, 14) == -1);
    bad = KIDS(2); bad.kid[1].birth_month = 13;
    CHECK(!kids_valid(&bad));
    return 0;
}

static int test_kids_turn(void)
{
    kids_t k = KIDS(2);

    // Deterministic per day. The morning draw, an early reveal and the 13:00
    // draw are three separate calls on one day; if they disagreed, the screen
    // would greet a different kid each time -- and, now that a guess is
    // credited to whoever the page named, would credit the wrong child.
    for (int32_t d = 0; d < 500; d++)
        CHECK(kids_turn_today(&k, d) == kids_turn_today(&k, d));

    // EVERY day names somebody, and only somebody who exists. This is the
    // whole change from the old hash-and-skip callout: a rotation, not a
    // lottery, so no child goes a week without the wall saying their name.
    for (int32_t d = 0; d < 600; d++) {
        const int i = kids_turn_today(&k, d);
        CHECK(i >= 0 && i < k.count);
    }

    // Strict rotation, and it comes round every kids_turn_period() days.
    CHECK(kids_turn_period(&k) == 2);
    for (int32_t d = 0; d < 200; d++)
        CHECK(kids_turn_today(&k, d) == kids_turn_today(&k, d + kids_turn_period(&k)));

    // Even shares over any whole number of cycles.
    int seen[KIDS_MAX] = {0};
    for (int32_t d = 0; d < 600; d++) seen[kids_turn_today(&k, d)]++;
    CHECK(seen[0] == 300 && seen[1] == 300);

    // FOUR KIDS AND A SEVEN-DAY WEEK ARE COPRIME, which is the reason strict
    // rotation is acceptable here at all: nobody is permanently stuck with
    // Mondays. Over four weeks each kid must see every weekday.
    kids_t four = KIDS(4);
    int wd_seen[4][7] = {{0}};
    for (int32_t d = 0; d < 28; d++) wd_seen[kids_turn_today(&four, d)][d % 7]++;
    for (int i = 0; i < 4; i++)
        for (int w = 0; w < 7; w++) CHECK(wd_seen[i][w] == 1);

    // One kid: always index 0, never -1.
    kids_t solo = KIDS(1);
    for (int32_t d = 0; d < 200; d++) CHECK(kids_turn_today(&solo, d) == 0);

    // No kids at all is the normal unconfigured state, and must not index.
    kids_t none;
    memset(&none, 0, sizeof none);
    CHECK(kids_turn_today(&none, 5) == -1);
    CHECK(kids_turn_period(&none) == 0);
    CHECK(kids_turn_today(NULL, 5) == -1);

    // A negative civil day would index off the front of the array with C's
    // truncating %, so the floor-modulo is load-bearing, not decoration.
    for (int32_t d = -50; d < 0; d++) {
        const int i = kids_turn_today(&k, d);
        CHECK(i >= 0 && i < k.count);
    }
    return 0;
}

// ---------------------------------------------------------------- weather ---

// The advice strings, by name. Duplicated from weather.c on purpose: a test
// that imports the value it is checking checks nothing.
#define ADV_GLOVES   "\xd7\x9e\xd7\xa2\xd7\x99\xd7\x9c \xd7\x95\xd7\x9b\xd7\xa4\xd7\xa4\xd7\x95\xd7\xaa"      // coat and gloves
#define ADV_RAINCOAT "\xd7\x9e\xd7\xa2\xd7\x99\xd7\x9c \xd7\x92\xd7\xa9\xd7\x9d"                                    // raincoat
#define ADV_UMBRELLA "\xd7\x9e\xd7\x98\xd7\xa8\xd7\x99\xd7\x99\xd7\x94"                                               // umbrella
#define ADV_WARMCOAT "\xd7\x9e\xd7\xa2\xd7\x99\xd7\x9c \xd7\x97\xd7\x9d"                                              // warm coat
#define ADV_COAT     "\xd7\x9e\xd7\xa2\xd7\x99\xd7\x9c"                                                                   // coat
#define ADV_SWEAT    "\xd7\xa1\xd7\x95\xd7\x95\xd7\x98\xd7\xa9\xd7\x99\xd7\xa8\xd7\x98"                           // sweatshirt
#define ADV_LONG     "\xd7\xa9\xd7\xa8\xd7\x95\xd7\x95\xd7\x9c \xd7\x90\xd7\xa8\xd7\x95\xd7\x9a"                // long sleeves
#define ADV_HAT      "\xd7\x9b\xd7\x95\xd7\x91\xd7\xa2 \xd7\x95\xd7\x9e\xd7\x99\xd7\x9d"                          // hat and water
#define ADV_TSHIRT   "\xd7\x97\xd7\x95\xd7\x9c\xd7\xa6\xd7\x94 \xd7\xa7\xd7\xa6\xd7\xa8\xd7\x94"                // short sleeves
//
// The fixture is a REAL open-meteo response captured on 2026-08-26, not a
// hand-written one. Hand-written fixtures encode what you believe the API
// returns; captured ones encode what it actually returns.
static const char *OM_REAL =
"{\"latitude\":32.0625,\"longitude\":34.8125,\"generationtime_ms\":0.089,"
"\"utc_offset_seconds\":10800,\"timezone\":\"Asia/Jerusalem\","
"\"timezone_abbreviation\":\"GMT+3\",\"elevation\":16.0,"
"\"current_units\":{\"time\":\"iso8601\",\"interval\":\"seconds\","
"\"temperature_2m\":\"°C\",\"weather_code\":\"wmo code\"},"
"\"current\":{\"time\":\"2026-08-26T23:00\",\"interval\":900,"
"\"temperature_2m\":26.6,\"weather_code\":0},"
"\"daily_units\":{\"time\":\"iso8601\",\"temperature_2m_max\":\"°C\","
"\"temperature_2m_min\":\"°C\",\"weather_code\":\"wmo code\"},"
"\"daily\":{\"time\":[\"2026-08-26\"],\"temperature_2m_max\":[31.9],"
"\"temperature_2m_min\":[23.7],\"weather_code\":[2]}}";

static int test_weather_parse(void)
{
    weather_t w;
    memset(&w, 0xAA, sizeof w);
    CHECK(weather_parse(OM_REAL, &w));
    CHECK(w.temp_x10 == 266);          // 26.6C
    CHECK(w.hi_x10   == 319);
    CHECK(w.lo_x10   == 237);
    CHECK(w.wmo      == 0);
    CHECK(w.fetched_at == 0);          // caller stamps this, not the parser

    // Garbage must not clobber a good cached reading.
    weather_t good = w;
    CHECK(!weather_parse("{ not json", &good));
    CHECK(good.temp_x10 == 266);
    CHECK(!weather_parse("{}", &good));
    CHECK(good.temp_x10 == 266);
    CHECK(!weather_parse(NULL, &good));
    CHECK(good.temp_x10 == 266);

    // A current block with no daily block is still usable.
    weather_t p;
    memset(&p, 0, sizeof p);
    CHECK(weather_parse("{\"current\":{\"temperature_2m\":-3.55,"
                        "\"weather_code\":71}}", &p));
    CHECK(p.wmo == 71);
    // Rounding half away from zero: a truncating cast would give -35 and put
    // the board a whole tenth warm on a freezing morning.
    CHECK(p.lo_x10 == 0 && p.hi_x10 == 0);
    CHECK(p.temp_x10 == -36);

    // open-meteo emits null in a daily array when a value is unavailable.
    weather_t n;
    memset(&n, 0, sizeof n);
    CHECK(weather_parse("{\"current\":{\"temperature_2m\":5,\"weather_code\":3},"
                        "\"daily\":{\"temperature_2m_max\":[null],"
                        "\"temperature_2m_min\":[]}}", &n));
    CHECK(n.temp_x10 == 50 && n.hi_x10 == 0 && n.lo_x10 == 0);
    return 0;
}

static int test_weather_icons(void)
{
    // Every icon named here must exist in page_weather/Weather_img, including
    // leiyu, which only exists because the lieyu transposition was fixed.
    CHECK(strcmp(wmo_icon(0),  "qin")    == 0);
    CHECK(strcmp(wmo_icon(2),  "duoyun") == 0);
    CHECK(strcmp(wmo_icon(3),  "yin")    == 0);
    CHECK(strcmp(wmo_icon(48), "wumai")  == 0);
    CHECK(strcmp(wmo_icon(61), "xiaoyu") == 0);
    CHECK(strcmp(wmo_icon(63), "zhongyu")== 0);
    CHECK(strcmp(wmo_icon(65), "dayu")   == 0);
    CHECK(strcmp(wmo_icon(82), "baoyu")  == 0);
    CHECK(strcmp(wmo_icon(75), "xiaxue") == 0);
    CHECK(strcmp(wmo_icon(95), "leiyu")  == 0);
    CHECK(strcmp(wmo_icon(99), "leiyu")  == 0);

    // Unknown codes fall back to a plausible cloud, never to nothing. WMO adds
    // codes and this board is not casually reflashable.
    CHECK(strcmp(wmo_icon(7),     "duoyun") == 0);
    CHECK(strcmp(wmo_icon(65535), "duoyun") == 0);
    for (uint32_t c = 0; c <= 120; c++) CHECK(wmo_icon((uint16_t)c) != NULL);
    CHECK(strcmp(wmo_label(0), "clear") == 0);
    CHECK(strcmp(wmo_label(7), "?")     == 0);
    return 0;
}

static int test_weather_advice(void)
{
    weather_t w;
    memset(&w, 0, sizeof w);

    // Precipitation outranks temperature: a warm rainy morning is an umbrella
    // morning, not a t-shirt morning. This is the branch that matters most --
    // a child told "t-shirt" in the rain stops reading the panel.
    w.temp_x10 = 260;
    w.wmo = 61;  CHECK(strcmp(weather_advice_he(&w), ADV_UMBRELLA)  == 0);
    w.wmo = 95;  CHECK(strcmp(weather_advice_he(&w), ADV_RAINCOAT)  == 0);
    w.wmo = 73;  CHECK(strcmp(weather_advice_he(&w), ADV_GLOVES)    == 0);

    // Temperature bands, on their boundaries. Each is the first value that
    // selects its band, so an off-by-one in either direction fails here.
    w.wmo = 0;
    w.temp_x10 =  99; CHECK(strcmp(weather_advice_he(&w), ADV_WARMCOAT) == 0);
    w.temp_x10 = 100; CHECK(strcmp(weather_advice_he(&w), ADV_COAT)     == 0);
    w.temp_x10 = 149; CHECK(strcmp(weather_advice_he(&w), ADV_COAT)     == 0);
    w.temp_x10 = 150; CHECK(strcmp(weather_advice_he(&w), ADV_SWEAT)    == 0);
    w.temp_x10 = 189; CHECK(strcmp(weather_advice_he(&w), ADV_SWEAT)    == 0);
    w.temp_x10 = 190; CHECK(strcmp(weather_advice_he(&w), ADV_LONG)     == 0);
    w.temp_x10 = 239; CHECK(strcmp(weather_advice_he(&w), ADV_LONG)     == 0);
    w.temp_x10 = 240; CHECK(strcmp(weather_advice_he(&w), ADV_TSHIRT)   == 0);
    w.temp_x10 = -50; CHECK(strcmp(weather_advice_he(&w), ADV_WARMCOAT) == 0);

    // The hot-afternoon override, and the trap under it: the daily block is
    // optional, so hi_x10 is zero whenever it was absent. A zero high must not
    // read as a cold afternoon, and must not suppress the hat either.
    w.temp_x10 = 280; w.hi_x10 = 340; CHECK(strcmp(weather_advice_he(&w), ADV_HAT)    == 0);
    w.temp_x10 = 280; w.hi_x10 = 300; CHECK(strcmp(weather_advice_he(&w), ADV_HAT)    == 0);
    w.temp_x10 = 280; w.hi_x10 = 299; CHECK(strcmp(weather_advice_he(&w), ADV_TSHIRT) == 0);
    w.temp_x10 = 280; w.hi_x10 =   0; CHECK(strcmp(weather_advice_he(&w), ADV_TSHIRT) == 0);
    // Already hotter than the forecast high: trust what is measured.
    w.temp_x10 = 330; w.hi_x10 = 320; CHECK(strcmp(weather_advice_he(&w), ADV_TSHIRT) == 0);

    CHECK(weather_advice_he(NULL)[0] == '\0');

    // THE TONE AND THE WORDS MUST NOT DRIFT. Every branch in
    // weather_advice_he() has one in weather_advice_tone(), and this walks the
    // same boundaries so a change to one that is not made to the other fails
    // here rather than on the wall as a blue heat warning.
    w.wmo = 0;
    w.temp_x10 =  99; w.hi_x10 = 0; CHECK(weather_advice_tone(&w) == WX_TONE_COLD);
    w.temp_x10 = 149;               CHECK(weather_advice_tone(&w) == WX_TONE_COLD);
    w.temp_x10 = 150;               CHECK(weather_advice_tone(&w) == WX_TONE_PLAIN);
    w.temp_x10 = 239;               CHECK(weather_advice_tone(&w) == WX_TONE_PLAIN);
    w.temp_x10 = 240;               CHECK(weather_advice_tone(&w) == WX_TONE_PLAIN);
    w.temp_x10 = 280; w.hi_x10 = 340; CHECK(weather_advice_tone(&w) == WX_TONE_HOT);
    w.temp_x10 = 280; w.hi_x10 = 299; CHECK(weather_advice_tone(&w) == WX_TONE_PLAIN);
    w.temp_x10 = 280; w.hi_x10 =   0; CHECK(weather_advice_tone(&w) == WX_TONE_PLAIN);
    w.temp_x10 = 260; w.hi_x10 = 0;
    w.wmo = 61; CHECK(weather_advice_tone(&w) == WX_TONE_WET);
    w.wmo = 95; CHECK(weather_advice_tone(&w) == WX_TONE_WET);
    w.wmo = 73; CHECK(weather_advice_tone(&w) == WX_TONE_COLD);   // snow is cold, not wet
    CHECK(weather_advice_tone(NULL) == WX_TONE_PLAIN);

    // Precipitation outranks temperature in BOTH, and the pairing is what
    // matters: an umbrella must never be drawn in the hat-and-water colour.
    w.temp_x10 = 330; w.hi_x10 = 350; w.wmo = 61;
    CHECK(strcmp(weather_advice_he(&w), ADV_UMBRELLA) == 0);
    CHECK(weather_advice_tone(&w) == WX_TONE_WET);

    // EVERY STRING MUST FIT THE LINE IT SHARES WITH THE TEMPERATURE.
    //
    // draw_line_rtl_fit elides an over-long line at its last space and appends
    // "..." -- which on this line lands exactly on the advice, turning "coat
    // and gloves" into "...". That failure is silent, only visible on the one
    // morning it matters, and 15 seconds of refresh away from being noticed.
    //
    // Measured at HE_W, the widest cell in the font, so a pass here holds for
    // every real glyph. The budget is the band's inner width, and "-10C " is
    // the widest temperature this board will ever print.
    const char *all[] = { ADV_UMBRELLA, ADV_RAINCOAT, ADV_GLOVES, ADV_WARMCOAT,
                          ADV_COAT, ADV_SWEAT, ADV_LONG, ADV_HAT, ADV_TSHIRT };
    he_metrics_t widest;
    for (int i = 0; i < HE_NGLYPH; i++) widest.width[i] = HE_W;
    const int budget = DL_CANVAS_W - 2 * (DL_MARGIN_X + DL_BAND_PAD);
    const int temp_w = 4 * HE_LAT_W + HE_SPACE;               // "-10C" plus its space
    for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++) {
        CHECK(all[i][0] != '\0');
        CHECK(he_measure(&widest, all[i]) + temp_w <= budget);
    }
    return 0;
}

static int test_weather_staleness(void)
{
    weather_t w;
    memset(&w, 0, sizeof w);

    CHECK(weather_is_stale(&w, 1000));            // never fetched
    CHECK(weather_is_stale(NULL, 1000));

    // The two intervals that actually occur, and the threshold sits between.
    w.fetched_at = 100000;
    CHECK(!weather_is_stale(&w, 100000 + 1));
    CHECK(!weather_is_stale(&w, 100000 + (uint32_t)(6.5 * 3600)));  // 06:30 -> 13:00
    CHECK(weather_is_stale(&w, 100000 + 24 * 3600));                // next morning, fetch failed
    CHECK(!weather_is_stale(&w, 100000 + WEATHER_STALE_SECS));      // boundary is inclusive
    CHECK(weather_is_stale(&w, 100000 + WEATHER_STALE_SECS + 1));

    // A clock that moved backwards means one of the two values is wrong and
    // there is no telling which, so it must not be presented as current. Note
    // this case is ALSO satisfied by unsigned underflow in the subtraction, so
    // this assertion does not distinguish the explicit guard from its absence;
    // the guard is kept for intent, not behaviour. See weather.c.
    CHECK(weather_is_stale(&w, 99999));
    CHECK(weather_is_stale(&w, 0));
    return 0;
}

// --------------------------------------------------------------- schedule ---
static int test_schedule_weekday(void)
{
    // Verified against a calendar, not derived. 1970-01-01 was a Thursday.
    CHECK(schedule_weekday(0)     == 4);   // 1970-01-01 Thu
    CHECK(schedule_weekday(20454) == 4);   // 2026-01-01 Thu
    CHECK(schedule_weekday(20692) == 4);   // 2026-08-27 Thu
    CHECK(schedule_weekday(20694) == 6);   // 2026-08-29 Sat
    CHECK(schedule_weekday(20695) == 0);   // 2026-08-30 Sun

    // A full week advances exactly one index and wraps once.
    for (int32_t d = 20692; d < 20692 + 14; d++)
        CHECK(schedule_weekday(d) == (int)((d + 4) % 7));
    // Pre-epoch must not index off the front of the array. C's % gives a
    // negative remainder for negative operands, which is the bug this guards.
    for (int32_t d = -400; d < 0; d++) {
        int w = schedule_weekday(d);
        CHECK(w >= 0 && w < SCHED_DAYS);
    }
    return 0;
}

static int test_weekday_he(void)
{
    // Distinct, non-empty, and Hebrew -- every string starts with the UTF-8
    // lead byte for the Hebrew block.
    for (int i = 0; i < 7; i++) {
        const char *n = schedule_weekday_he(i);
        CHECK(n && n[0] == '\xd7');
        for (int j = 0; j < i; j++) CHECK(strcmp(n, schedule_weekday_he(j)) != 0);
    }
    // Out of range clamps rather than indexing off the end. The caller passes
    // schedule_weekday() output, which is arithmetic, not a checked enum.
    CHECK(schedule_weekday_he(-1) == schedule_weekday_he(0));
    CHECK(schedule_weekday_he(7)  == schedule_weekday_he(0));
    CHECK(schedule_weekday_he(99) == schedule_weekday_he(0));

    // 20692 is 2026-08-27, a Thursday -- the same date the firmware asserts
    // against at boot. Thursday is 4 with Sunday as 0.
    CHECK(strcmp(schedule_weekday_he(schedule_weekday(20692)),
                 schedule_weekday_he(4)) == 0);
    return 0;
}

static int test_schedule_parse(void)
{
    schedule_t s;
    memset(&s, 0xAA, sizeof s);

    // Hebrew subjects, days named rather than indexed.
    const char *doc =
        "{\"days\":{"
        "\"sun\":[\"מתמטיקה\",\"אנגלית\"],"
        "\"thu\":[\"ספורט\"]"
        "}}";
    CHECK(schedule_parse(doc, &s));
    CHECK(!schedule_is_empty(&s));

    // Sunday joins with an ASCII separator, because the middle dot the design
    // doc used is not drawable by hebrew.inc.
    CHECK(strcmp(s.line[0], "מתמטיקה, אנגלית") == 0);
    CHECK(strcmp(s.line[4], "ספורט") == 0);
    CHECK(s.line[1][0] == 0);                      // omitted day, empty
    CHECK(strcmp(schedule_for_day(&s, 20695), "מתמטיקה, אנגלית") == 0);  // a Sunday
    CHECK(strcmp(schedule_for_day(&s, 20694), "") == 0);                  // a Saturday
    CHECK(strcmp(schedule_for_day(NULL, 20695), "") == 0);

    // Undrawable subjects are skipped, not rendered as holes. The rest of the
    // day survives.
    schedule_t u;
    memset(&u, 0, sizeof u);
    CHECK(schedule_parse("{\"days\":{\"mon\":[\"שָלום\",\"ספורט\"]}}", &u));
    CHECK(strcmp(u.line[1], "ספורט") == 0);

    // A document that is not a schedule leaves a good one untouched.
    schedule_t good = s;
    CHECK(!schedule_parse("{ not json", &good));
    CHECK(strcmp(good.line[0], "מתמטיקה, אנגלית") == 0);
    CHECK(!schedule_parse("{}", &good));
    CHECK(strcmp(good.line[0], "מתמטיקה, אנגלית") == 0);
    CHECK(!schedule_parse(NULL, &good));

    // Empty is legal and means the zone does not draw.
    schedule_t e;
    memset(&e, 0, sizeof e);
    CHECK(schedule_parse("{\"days\":{}}", &e));
    CHECK(schedule_is_empty(&e));
    CHECK(schedule_is_empty(NULL));

    // An over-long day stops cleanly at a subject boundary rather than
    // emitting half a word.
    schedule_t o;
    memset(&o, 0, sizeof o);
    CHECK(schedule_parse("{\"days\":{\"sun\":["
        "\"אאאאאאאאאא\",\"בבבבבבבבבב\",\"גגגגגגגגגג\",\"דדדדדדדדדד\","
        "\"הההההההההה\",\"וווווווווו\",\"זזזזזזזזזז\",\"חחחחחחחחחח\"]}}", &o));
    CHECK(strlen(o.line[0]) < SCHED_LINE_MAX);
    CHECK(o.line[0][strlen(o.line[0]) - 1] != ' ');   // no dangling separator
    return 0;
}

// ----------------------------------------------------------- riddle_batch ---
//
// These rules used to live inside page_riddle.cc with ESP_LOG threaded through
// them, so none could be checked without a board. Each one is a decision.
static int test_riddle_batch(void)
{
    riddle_batch_t b;

    // A whole riddle, with choices that contain the answer.
    const char *good =
        "{\"riddles\":[{\"q\":\"Q1\",\"a\":\"sun\","
        "\"choices\":[\"sun\",\"rain\",\"age\"],\"by\":\"dad\"}]}";
    CHECK(riddle_batch_parse(good, &b) == 1);
    CHECK(b.count == 1 && b.skipped == 0);
    CHECK(strcmp(b.item[0].q, "Q1") == 0);
    CHECK(strcmp(b.item[0].a, "sun") == 0);
    CHECK(strcmp(b.item[0].by, "dad") == 0);
    CHECK(b.item[0].has_choices);
    CHECK(!b.item[0].weekend);

    // THE UNWINNABLE-CHOICES GUARD. Three options none of which is the answer
    // would mark every guess wrong. Fall back to a plain reveal instead.
    const char *unwinnable =
        "{\"riddles\":[{\"q\":\"Q\",\"a\":\"sun\","
        "\"choices\":[\"rain\",\"age\",\"moon\"]}]}";
    CHECK(riddle_batch_parse(unwinnable, &b) == 1);
    CHECK(!b.item[0].has_choices);

    // Wrong number of choices, or an empty one: same fallback, still usable.
    const char *twoch = "{\"riddles\":[{\"q\":\"Q\",\"a\":\"x\",\"choices\":[\"x\",\"y\"]}]}";
    CHECK(riddle_batch_parse(twoch, &b) == 1 && !b.item[0].has_choices);
    const char *emptych =
        "{\"riddles\":[{\"q\":\"Q\",\"a\":\"x\",\"choices\":[\"x\",\"\",\"z\"]}]}";
    CHECK(riddle_batch_parse(emptych, &b) == 1 && !b.item[0].has_choices);

    // THE KINDS THE RENDERER KNOWS. joke/word/math used to be skipped along
    // with everything unrecognised; they are content types now, because a page
    // whose shape never changes stops being looked at. An absent type still
    // means riddle, and a genuinely unknown one is still skipped rather than
    // drawn as a riddle nobody can solve.
    const char *typed =
        "{\"riddles\":[{\"type\":\"joke\",\"q\":\"J\",\"a\":\"A\"},"
        "{\"type\":\"riddle\",\"q\":\"R\",\"a\":\"A\"},"
        "{\"type\":\"word\",\"q\":\"W\",\"a\":\"A\"},"
        "{\"type\":\"math\",\"q\":\"M\",\"a\":\"A\"},"
        "{\"q\":\"D\",\"a\":\"A\"},"
        "{\"type\":\"limerick\",\"q\":\"L\",\"a\":\"A\"}]}";
    CHECK(riddle_batch_parse(typed, &b) == 5);
    CHECK(b.skipped == 1);                       // only the limerick
    CHECK(b.item[0].kind == RK_JOKE   && strcmp(b.item[0].q, "J") == 0);
    CHECK(b.item[1].kind == RK_RIDDLE && strcmp(b.item[1].q, "R") == 0);
    CHECK(b.item[2].kind == RK_WORD);
    CHECK(b.item[3].kind == RK_MATH);
    CHECK(b.item[4].kind == RK_RIDDLE);          // absent type means riddle

    // WHY THE ANSWER IS THE ANSWER. Optional: absent leaves an empty string,
    // and the reveal then draws exactly the page it drew before this existed.
    const char *withwhy =
        "{\"riddles\":[{\"q\":\"Q\",\"a\":\"A\",\"why\":\"because\"},"
        "{\"q\":\"Q2\",\"a\":\"A2\"}]}";
    CHECK(riddle_batch_parse(withwhy, &b) == 2);
    CHECK(strcmp(b.item[0].why, "because") == 0);
    CHECK(b.item[1].why[0] == '\0');

    // ONE BAD ENTRY COSTS THAT ENTRY, NOT THE BATCH. A missing answer in the
    // middle of thirty riddles should not cost the month.
    const char *mixed =
        "{\"riddles\":[{\"q\":\"A\",\"a\":\"1\"},{\"q\":\"no answer\"},"
        "{\"q\":\"C\",\"a\":\"3\"}]}";
    CHECK(riddle_batch_parse(mixed, &b) == 2);
    CHECK(b.skipped == 1);
    CHECK(strcmp(b.item[1].q, "C") == 0);

    // weekend is carried through.
    const char *wk = "{\"riddles\":[{\"q\":\"Q\",\"a\":\"A\",\"weekend\":true}]}";
    CHECK(riddle_batch_parse(wk, &b) == 1 && b.item[0].weekend);

    // Documents that are unusable as documents fail, and are distinguishable
    // from an empty batch: -1 versus 0.
    CHECK(riddle_batch_parse("not json", &b) == -1);
    CHECK(riddle_batch_parse("{\"nope\":[]}", &b) == -1);
    CHECK(riddle_batch_parse(NULL, &b) == -1);
    CHECK(riddle_batch_parse("{\"riddles\":[]}", &b) == 0);

    // An over-long question truncates rather than dropping the riddle: clipped
    // is visible and survivable, missing is not.
    static char big[RB_Q_MAX + 400];
    int k = snprintf(big, sizeof big, "{\"riddles\":[{\"q\":\"");
    for (int i = 0; i < RB_Q_MAX + 100; i++) big[k++] = 'x';
    snprintf(big + k, sizeof big - k, "\",\"a\":\"A\"}]}");
    CHECK(riddle_batch_parse(big, &b) == 1);
    CHECK(strlen(b.item[0].q) == RB_Q_MAX - 1);

    // The cap is honoured rather than overrunning the array.
    static char many[8192];
    k = snprintf(many, sizeof many, "{\"riddles\":[");
    for (int i = 0; i < RB_MAX + 5; i++)
        k += snprintf(many + k, sizeof many - k, "%s{\"q\":\"Q\",\"a\":\"A\"}",
                      i ? "," : "");
    snprintf(many + k, sizeof many - k, "]}");
    CHECK(riddle_batch_parse(many, &b) == RB_MAX);

    return 0;
}

// ---------------------------------------------------------------- he_text ---
//
// Word wrapping used to live inside hebrew.inc next to the pixel blitting, so
// the only way to check it was to look at a panel. On this board that costs 17
// seconds an attempt. Synthetic metrics -- every letter 10px wide -- make the
// arithmetic exact and the expectations readable.
static int test_he_text(void)
{
    he_metrics_t m;
    for (int i = 0; i < HE_NGLYPH; i++) m.width[i] = 10;

    // UTF-8 decoding, including the Hebrew two-byte range.
    uint32_t cp;
    CHECK(he_utf8_next("", &cp) == 0);
    CHECK(he_utf8_next("A", &cp) == 1 && cp == 'A');
    CHECK(he_utf8_next("\xd7\x90", &cp) == 2 && cp == 0x05D0);   // alef
    CHECK(he_is_hebrew(0x05D0));
    CHECK(!he_is_hebrew('A'));

    // Advances: Hebrew is ink width plus the gap, space and Latin are fixed,
    // and anything undrawable advances zero rather than leaving a hole.
    CHECK(he_advance(&m, 0x05D0) == 10 + HE_GAP);
    CHECK(he_advance(&m, ' ') == HE_SPACE);
    CHECK(he_advance(&m, 'A') == HE_LAT_W);
    CHECK(he_advance(&m, 0x00B7) == 0);       // middle dot: not drawable
    CHECK(he_advance(&m, 0x05B4) == 0);       // niqqud: outside the glyph range

    // Two alefs and a space = 13 + 9 + 13.
    CHECK(he_measure(&m, "\xd7\x90 \xd7\x90") == 13 + 9 + 13);
    CHECK(he_measure(&m, "") == 0);
    CHECK(he_measure(&m, NULL) == 0);

    // Breaking on a space: "alef alef" in 30px fits only the first word.
    const char *two = "\xd7\x90 \xd7\x90";
    CHECK(he_line_break(&m, two, 100) == (int)strlen(two));   // all of it
    CHECK(he_line_break(&m, two, 30) == 2);                   // just the alef

    // THE BAIL-OUT THAT MATTERS. A single word wider than the whole line has no
    // space to break at. Returning 0 would stall the caller forever, so the
    // over-wide word is emitted whole and allowed to overhang.
    const char *longword = "\xd7\x90\xd7\x90\xd7\x90\xd7\x90";
    int n = he_line_break(&m, longword, 5);
    CHECK(n > 0);
    CHECK(n == (int)strlen(longword));

    // Degenerate inputs must not loop or crash.
    CHECK(he_line_break(&m, "", 100) == 0);
    CHECK(he_line_break(&m, NULL, 100) == 0);
    CHECK(he_line_break(&m, two, 0) == 0);

    // A leading space must not be chosen as the break point -- that returns
    // zero progress and hangs the wrap loop.
    CHECK(he_line_break(&m, " \xd7\x90", 12) > 0);

    // Missing width falls back to the full cell rather than zero, so a bad
    // table renders wide instead of overlapping into gibberish.
    he_metrics_t zero;
    for (int i = 0; i < HE_NGLYPH; i++) zero.width[i] = 0;
    CHECK(he_glyph_width(&zero, 0x05D0) == HE_W);
    CHECK(he_glyph_width(&m, 'A') == 0);      // not Hebrew: no glyph
    CHECK(he_glyph_width(NULL, 0x05D0) == 0);

    return 0;
}

// ----------------------------------------------------------- daily_layout ---
//
// Sixteen flag combinations, checked against the properties a reflow bug
// breaks rather than against pixel numbers, which will be nudged against the
// real panel. A loop rather than sixteen hand-written cases: the hand-written
// set is exactly where the awkward combination goes missing.
static int test_daily_layout(void)
{
    daily_flags_t empty = { false, false, false, false };
    daily_flags_t full  = { true,  true,  true,  true  };
    daily_layout_t Le, Lf;
    daily_layout(&empty, &Le);
    daily_layout(&full,  &Lf);

    for (int bits = 0; bits < 16; bits++) {
        daily_flags_t f;
        f.schedule = (bits & 1) != 0;
        f.weather  = (bits & 2) != 0;
        f.callout  = (bits & 4) != 0;
        f.birthday = (bits & 8) != 0;

        daily_layout_t L;
        daily_layout(&f, &L);

        // Absent means absent, and takes no space. Every zone is placed when
        // asked for -- portrait has room, so unlike the landscape version
        // nothing is suppressed to make things fit.
        CHECK(f.schedule == (L.schedule_y != DL_ABSENT));
        CHECK(f.weather  == (L.weather_y  != DL_ABSENT));
        // A BIRTHDAY REPLACES THE TURN LINE. Both address the reader by name,
        // and on the one morning a child's name is on the page in red, telling
        // a different child that today's riddle is theirs is the wrong page.
        CHECK((f.callout && !f.birthday) == (L.callout_y != DL_ABSENT));
        CHECK(f.birthday == (L.birthday_y != DL_ABSENT));

        // The birthday label is placed exactly with the birthday name.
        CHECK((L.birthday_label_y != DL_ABSENT) == (L.birthday_y != DL_ABSENT));
        if (L.birthday_y != DL_ABSENT)
            CHECK(L.birthday_y >= L.birthday_label_y + DL_SMALL_H);

        // The lead rule is always placed, always below every zone, and always
        // above the riddle. An empty ruled band is the failure this replaced.
        CHECK(L.lead_rule_y != DL_ABSENT);
        CHECK(L.riddle_top >= L.lead_rule_y + DL_LEAD_RULE_H);
        if (L.callout_y  != DL_ABSENT) CHECK(L.lead_rule_y >= L.callout_y + DL_LINE_H);
        if (L.birthday_y != DL_ABSENT) CHECK(L.lead_rule_y >= L.birthday_y + DL_LINE_H);
        if (L.band_h > 0)              CHECK(L.lead_rule_y >= L.band_y + L.band_h);

        // Nothing starts above the header rule; nothing runs off the page.
        if (L.schedule_y != DL_ABSENT) CHECK(L.schedule_y > DL_HDR_RULE_Y);
        if (L.weather_y  != DL_ABSENT) CHECK(L.weather_y  > DL_HDR_RULE_Y);
        if (L.birthday_y != DL_ABSENT) CHECK(L.birthday_y > DL_HDR_RULE_Y);
        if (L.callout_y  != DL_ABSENT) CHECK(L.callout_y  > DL_HDR_RULE_Y);
        // The riddle zone ends above the folio, not at the page edge. A
        // printed page ends deliberately; this one used to stop wherever the
        // last choice fell.
        CHECK(L.riddle_top + L.riddle_h == DL_FOLIO_Y - DL_ZONE_GAP);
        CHECK(DL_FOLIO_Y + DL_FOLIO_H <= DL_BODY_BOTTOM);
        CHECK(L.riddle_top < DL_CANVAS_H);

        // SIDE BY SIDE, and this assertion used to say the opposite: "stacked,
        // not side by side: weather sits a full line below schedule." That was
        // right while the weather was a line of text -- two text columns on a
        // 400px panel give each 190px, which will not hold a Hebrew timetable.
        // The weather is a 126px PANEL now, which leaves the timetable 232px
        // and two lines to wrap into: more room in total than the single full
        // width line it used to get.
        if (L.schedule_y != DL_ABSENT && L.weather_y != DL_ABSENT) {
            CHECK(L.schedule_y < L.weather_y + DL_WXBOX_H);   // overlapping rows
            CHECK(L.band_h >= DL_WXBOX_H);                    // band holds the box
        }

        // Order and non-overlap.
        // They can no longer coexist, which is the point of the change.
        CHECK(!(L.birthday_y != DL_ABSENT && L.callout_y != DL_ABSENT));
        if (L.callout_y != DL_ABSENT)
            CHECK(L.riddle_top >= L.callout_y + DL_LINE_H);
        if (L.birthday_y != DL_ABSENT)
            CHECK(L.riddle_top >= L.birthday_y + DL_LINE_H);

        if (!f.schedule && !f.weather) {
            CHECK(L.band_h == 0);
        } else {
            CHECK(L.band_h > 0);
            if (L.schedule_y != DL_ABSENT) {
                CHECK(L.schedule_y >= L.band_y);
                CHECK(L.schedule_y + DL_LINE_H <= L.band_y + L.band_h);
            }
            if (L.weather_y != DL_ABSENT) {
                CHECK(L.weather_y >= L.band_y);
                CHECK(L.weather_y + DL_WXBOX_H <= L.band_y + L.band_h);
            }
            // Everything below the band clears it. Checking only riddle_top is
            // not enough: the eye-level floor sits below any band this layout
            // produces, so it would clear the band by accident even if the
            // band's height were ignored entirely -- which is exactly how a
            // mutation survived on the Waveshare board.
            if (L.birthday_y != DL_ABSENT)
                CHECK(L.birthday_y >= L.band_y + L.band_h);
            if (L.callout_y != DL_ABSENT)
                CHECK(L.callout_y >= L.band_y + L.band_h);
            CHECK(L.riddle_top >= L.band_y + L.band_h);
        }

        CHECK(L.riddle_h >= DL_RIDDLE_MIN_H);
        CHECK(L.riddle_top >= Le.riddle_top);
        CHECK(L.riddle_top <= Lf.riddle_top);
    }

    CHECK(Le.riddle_top <= Lf.riddle_top);
    CHECK(Le.riddle_h   >= Lf.riddle_h);
    CHECK(Le.band_h == 0);

    // NO ORPHAN BAND. This used to assert the riddle floor (200) held on an
    // empty page. The floor is gone, and this is the property that replaced
    // it: on a page with nothing to say, the lead rule sits immediately below
    // the masthead rather than 50px under it with nothing in between. That gap
    // was a ruled band containing nothing, which reads as a story that failed
    // to load. Vertical placement of the riddle is page_daily's job now -- it
    // centres the measured block -- not a constant's.
    CHECK(Le.band_h == 0);
    CHECK(Le.lead_rule_y == DL_HDR_RULE_Y + DL_HDR_RULE_H + DL_ZONE_GAP);
    CHECK(Le.riddle_top == Le.lead_rule_y + DL_LEAD_RULE_H + 10);

    // A BIRTHDAY SUPPRESSES THE TURN LINE, and this assertion used to say the
    // opposite: "a birthday does NOT suppress the callout here -- portrait has
    // the room." Portrait does have the room; the page does not want it. Both
    // lines address the reader by name, and two of those on one morning is one
    // too many. It also buys back the 47px that made the crowded day the case
    // nothing else could be fitted into.
    daily_flags_t both = { true, true, true, true };
    daily_layout_t Lt;
    daily_layout(&both, &Lt);
    CHECK(Lt.birthday_y != DL_ABSENT);
    CHECK(Lt.callout_y == DL_ABSENT);

    // Zones must actually push the page down, or a reflow bug that ignored
    // them entirely would still pass everything above.
    CHECK(Lt.riddle_top > Le.riddle_top);
    CHECK(Lt.lead_rule_y > Le.lead_rule_y);

    // ---- today's picture, in the slack -------------------------------------
    //
    // It fills space the riddle block is not using and never takes any. The
    // old design gave it a zone under the masthead that pushed everything
    // down; the newspaper masthead and the boxed weather panel then took 49px
    // between them and left a normal school day 45px of picture. A sliver is
    // not an illustration, and a feature that stops firing is worse than one
    // that was never written.
    daily_flags_t day = { true, true, true, false };
    daily_layout_t Ld;
    daily_layout(&day, &Ld);

    // A short riddle leaves real slack, and the picture takes it. 56 is what
    // the generator publishes by default -- the folio took 31px off the zone,
    // so 90 no longer fits an ordinary weekday and 56 comfortably does.
    CHECK(daily_image_in_slack(Ld.riddle_top, Ld.riddle_h, 200, 56) != DL_ABSENT);
    CHECK(daily_image_in_slack(Ld.riddle_top, Ld.riddle_h, 200, 56)
          == Ld.riddle_top + DL_ZONE_GAP);
    // And the tallest the format allows does not, on that same day, which is
    // the self-regulating part: a big band appears only on a light page.
    CHECK(daily_image_in_slack(Ld.riddle_top, Ld.riddle_h, 200, STRIP_H_MAX) == DL_ABSENT);

    // A block that fills the zone leaves none, and there is no picture. This
    // is the case the old design got wrong by reserving space up front.
    CHECK(daily_image_in_slack(Ld.riddle_top, Ld.riddle_h, Ld.riddle_h, 56) == DL_ABSENT);

    // The boundary: the picture needs a gap above AND below it, so it fits at
    // exactly image_h + 2 gaps of slack and not one pixel less.
    const int need = 56 + 2 * DL_ZONE_GAP;
    CHECK(daily_image_in_slack(100, 400, 400 - need, 56) != DL_ABSENT);
    CHECK(daily_image_in_slack(100, 400, 400 - need + 1, 56) == DL_ABSENT);

    // No picture asked for, no picture placed -- and a negative height is a
    // corrupt strip header, not a request.
    CHECK(daily_image_in_slack(100, 400, 100, 0) == DL_ABSENT);
    CHECK(daily_image_in_slack(100, 400, 100, -5) == DL_ABSENT);
    CHECK(daily_image_in_slack(100, 400, 100, 0) == DL_ABSENT);

    // AND IT NEVER PUSHES THE RIDDLE ANYWHERE. Whatever the zones and whatever
    // the picture, the riddle keeps its floor, because the picture is no
    // longer part of that arithmetic at all.
    for (int bits = 0; bits < 16; bits++) {
        daily_flags_t g;
        g.schedule = (bits & 1) != 0; g.weather  = (bits & 2) != 0;
        g.callout  = (bits & 4) != 0; g.birthday = (bits & 8) != 0;
        daily_layout_t Lg;
        daily_layout(&g, &Lg);
        CHECK(Lg.riddle_h >= DL_RIDDLE_MIN_H);
        for (int hh = 0; hh <= STRIP_H_MAX; hh += 8) {
            const int iy = daily_image_in_slack(Lg.riddle_top, Lg.riddle_h, 200, hh);
            if (iy != DL_ABSENT) {
                CHECK(iy >= Lg.riddle_top);
                CHECK(iy + hh <= Lg.riddle_top + Lg.riddle_h);
            }
        }
    }

    // NULL flags behave as the empty page.
    daily_layout_t Ln;
    daily_layout(NULL, &Ln);
    CHECK(Ln.riddle_top == Le.riddle_top);
    CHECK(Ln.band_h == 0);

    return 0;
}

// ---------------------------------------------------------------- sd_json ---
static int test_sd_json(void)
{
    char buf[64];
    size_t len = 12345;

    CHECK(sdj_read("/nonexistent/nope.json", buf, sizeof buf, &len) == SDJ_ABSENT);
    CHECK(len == 0);

    const char *tmp = "/tmp/gstack-sdj-test.json";
    FILE *f = fopen(tmp, "w"); CHECK(f != NULL);
    fputs("{\"a\":1}", f); fclose(f);
    CHECK(sdj_read(tmp, buf, sizeof buf, &len) == SDJ_OK);
    CHECK(len == 7 && strcmp(buf, "{\"a\":1}") == 0);

    // Empty file is distinguishable from absent.
    f = fopen(tmp, "w"); CHECK(f != NULL); fclose(f);
    CHECK(sdj_read(tmp, buf, sizeof buf, &len) == SDJ_EMPTY);

    // THE POINT: an oversized file is TOO_BIG, not a silent fragment that then
    // fails to parse and reports the wrong cause.
    f = fopen(tmp, "w"); CHECK(f != NULL);
    for (int i = 0; i < 200; i++) fputc('x', f);
    fclose(f);
    CHECK(sdj_read(tmp, buf, sizeof buf, &len) == SDJ_TOO_BIG);
    CHECK(buf[0] == 0);            // no fragment handed back
    CHECK(len == 0);

    // Exactly filling the buffer is OK, one over is not.
    f = fopen(tmp, "w"); for (int i = 0; i < 63; i++) fputc('y', f); fclose(f);
    CHECK(sdj_read(tmp, buf, sizeof buf, &len) == SDJ_OK && len == 63);
    f = fopen(tmp, "w"); for (int i = 0; i < 64; i++) fputc('y', f); fclose(f);
    CHECK(sdj_read(tmp, buf, sizeof buf, &len) == SDJ_TOO_BIG);

    CHECK(sdj_read(NULL, buf, sizeof buf, &len) == SDJ_IO);
    CHECK(sdj_read(tmp, buf, 1, &len) == SDJ_IO);
    CHECK(strcmp(sdj_strerror(SDJ_TOO_BIG), "file is larger than the buffer") == 0);
    remove(tmp);
    return 0;
}

// --------------------------------------------------------------- formdata ---
static int test_formdata(void)
{
    char v[32];

    // Ordinary fields, first / middle / last.
    const char *b = "ssid=home&pass=abc&d0=x";
    CHECK(form_field(b, "ssid", v, sizeof v) && strcmp(v, "home") == 0);
    CHECK(form_field(b, "pass", v, sizeof v) && strcmp(v, "abc") == 0);
    CHECK(form_field(b, "d0",   v, sizeof v) && strcmp(v, "x") == 0);
    CHECK(!form_field(b, "d1", v, sizeof v));

    // THE ANCHORING BUG. "d1" must not be found inside "k0d1" or as the tail
    // of "kd1", and a name that is a prefix of another must not match it.
    CHECK(!form_field("k0d1=7", "d1", v, sizeof v));
    CHECK(form_field("k0d1=7&d1=9", "d1", v, sizeof v) && strcmp(v, "9") == 0);
    CHECK(form_field("d0=a&d0x=b", "d0", v, sizeof v) && strcmp(v, "a") == 0);
    CHECK(form_field("ssidx=no&ssid=yes", "ssid", v, sizeof v) &&
          strcmp(v, "yes") == 0);

    // An empty value is present, not absent -- clearing a day is how a parent
    // says "nothing on Friday", and reading it as absent would keep the old
    // line forever.
    CHECK(form_field("d5=&d6=x", "d5", v, sizeof v) && v[0] == '\0');

    // Percent and plus decoding. A password may legitimately contain either.
    CHECK(form_field("pass=a+b%26c", "pass", v, sizeof v) &&
          strcmp(v, "a b&c") == 0);
    CHECK(form_field("pass=100%25%20sure", "pass", v, sizeof v) &&
          strcmp(v, "100% sure") == 0);
    // A stray '%' with nothing usable after it is kept, not silently eaten.
    CHECK(form_field("pass=50%zz", "pass", v, sizeof v) &&
          strcmp(v, "50%zz") == 0);

    // Hebrew survives a round trip. "אב" is %D7%90%D7%91.
    CHECK(form_field("d0=%D7%90%D7%91", "d0", v, sizeof v) &&
          strcmp(v, "\xd7\x90\xd7\x91") == 0);

    // THE HALF-CHARACTER BUG. Truncation counts urlencoded bytes, so a value
    // that does not fit gets cut mid-letter. What lands must still be valid
    // UTF-8 -- shorter is fine, a lead byte with no continuation is not.
    {
        // TRUNCATION IS MEASURED IN DECODED BYTES, not urlencoded ones. Three
        // Hebrew letters are 18 characters on the wire and 6 in memory, so they
        // fit an 8-byte buffer. Measuring the encoded form is what stored
        // "איתי" as "אית" and cut the last subject off the timetable.
        char small[8];
        CHECK(form_field("d0=%D7%90%D7%91%D7%92", "d0", small, sizeof small));
        CHECK(strcmp(small, "\xd7\x90\xd7\x91\xd7\x92") == 0);
    }
    {
        // Still truncated when the DECODED text genuinely does not fit, and
        // still on a character boundary.
        char small[6];
        CHECK(form_field("d0=%D7%90%D7%91%D7%92", "d0", small, sizeof small));
        CHECK(strcmp(small, "\xd7\x90\xd7\x91") == 0);
    }
    {
        // "x" plus one Hebrew letter is 3 decoded bytes; a 4-byte buffer holds
        // it exactly.
        char small[4];
        CHECK(form_field("d0=x%D7%90", "d0", small, sizeof small));
        CHECK(strcmp(small, "x\xd7\x90") == 0);
    }
    {
        // Two euro signs are 6 decoded bytes; a 7-byte buffer holds both.
        char small[7];
        CHECK(form_field("d0=%E2%82%AC%E2%82%AC", "d0", small, sizeof small));
        CHECK(strcmp(small, "\xe2\x82\xac\xe2\x82\xac") == 0);
    }
    {
        // Room for one: the second is dropped whole, never half.
        char small[5];
        CHECK(form_field("d0=%E2%82%AC%E2%82%AC", "d0", small, sizeof small));
        CHECK(strcmp(small, "\xe2\x82\xac") == 0);
    }
    {
        // Too small for even one: empty, not a lead byte with no continuation.
        char tiny[3];
        CHECK(form_field("d0=%E2%82%AC", "d0", tiny, sizeof tiny));
        CHECK(tiny[0] == '\0');
    }

    // Nothing dereferences a null.
    CHECK(!form_field(NULL, "d0", v, sizeof v));
    CHECK(!form_field("d0=x", NULL, v, sizeof v));
    CHECK(!form_field("d0=x", "d0", NULL, sizeof v));
    CHECK(!form_field("d0=x", "d0", v, 0));

    // A name too long for the internal key buffer is refused, not truncated
    // into a match against something else.
    CHECK(!form_field("d0=x", "a_very_long_field_name_indeed", v, sizeof v));
    return 0;
}

// Measures a line against a font blob read off disk, with that cut's own
// metrics. he_measure() cannot do this: its HE_GAP / HE_SPACE / HE_LAT_W are
// the BODY cut's constants, and the masthead needs the small cut too.
// Mirrors he::face in ui/hebrew.cpp -- keep the numbers in step.
struct face_m { unsigned char w[27]; int cell_w, gap, space, lat; };

static int load_face(const char *path, int cell_w, int cell_h,
                     int gap, int space, int lat, struct face_m *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    const long glyph = (long)(cell_w / 8) * cell_h;
    if (fseek(f, glyph * 27, SEEK_SET) != 0) { fclose(f); return 0; }
    const size_t got = fread(out->w, 1, 27, f);
    fclose(f);
    if (got != 27) return 0;
    out->cell_w = cell_w; out->gap = gap; out->space = space; out->lat = lat;
    return 1;
}

static int face_measure(const struct face_m *f, const char *s)
{
    int w = 0;
    uint32_t cp;
    int n;
    while ((n = he_utf8_next(s, &cp)) > 0) {
        if (he_is_letter(cp))              w += f->w[cp - HE_BASE] + f->gap;
        else if (cp == ' ')                w += f->space;
        else if (cp > 0x20 && cp < 0x7F)   w += f->lat;
        s += n;
    }
    return w;
}

static int test_masthead_fits(void)
{
    // Real blobs, not a fabricated width table: this test exists to catch a
    // collision measured in single pixels, so an approximation would defeat it.
    struct face_m body, small;
    if (!load_face("main/ui/font24HE.FON", 24, 41, HE_GAP, HE_SPACE, HE_LAT_W, &body) ||
        !load_face("main/ui/font16HE.FON", 16, 24, 2, 5, 12, &small)) {
        // Run from somewhere else; skip rather than fail. `make test` runs
        // from the papercolor root, which is where this bites.
        return 0;
    }

    // THE WIDEST DATELINE THE PAGE CAN PRODUCE. Longest weekday, a two-digit
    // date, and an issue number with room to grow -- the collision this guards
    // was invisible at issue 1 and real at issue 100.
    int widest_day = 0;
    for (int i = 0; i < 7; i++) {
        const int w = face_measure(&small, schedule_weekday_he(i));
        if (w > widest_day) widest_day = w;
    }
    char rest[64];
    snprintf(rest, sizeof rest,
             " \xc2\xb7 31/08 \xc2\xb7 \xd7\x92\xd7\x99\xd7\x9c\xd7\x99\xd7\x95\xd7\x9f 9999");
    const int dateline_w = widest_day + face_measure(&small, rest);

    // Geometry, exactly as draw_header draws it: the dateline is anchored at
    // the left margin, the nameplate runs right-to-left from the right one.
    const int dateline_right = DL_MARGIN_X + dateline_w;
    const int name_left = (DL_CANVAS_W - DL_MARGIN_X)
                          - face_measure(&body, MASTHEAD_NAME);

    CHECK(name_left - dateline_right >= MASTHEAD_MIN_GAP);

    // And the nameplate alone must not run off the left edge, which
    // draw_line_rtl would answer by silently dropping its first letters.
    CHECK(face_measure(&body, MASTHEAD_NAME) <= DL_CANVAS_W - 2 * DL_MARGIN_X);

    // The name this replaced. Kept as a live assertion rather than a comment:
    // it is the shape of the bug, and a future change that makes it pass again
    // has reintroduced the collision.
    const char *old_name = "\xd7\x97\xd7\x99\xd7\x93\xd7\xaa \xd7\x94\xd7\x91\xd7\x95\xd7\xa7\xd7\xa8";
    const int old_left = (DL_CANVAS_W - DL_MARGIN_X) - face_measure(&body, old_name);
    CHECK(old_left - dateline_right < MASTHEAD_MIN_GAP);
    return 0;
}

static int test_strip(void)
{
    // A 400x4 strip: header plus 200 bytes a row.
    const size_t stride = STRIP_W / 2;
    static uint8_t buf[STRIP_HDR_BYTES + (STRIP_W / 2) * 4];
    memcpy(buf, STRIP_MAGIC, 4);
    buf[4] = STRIP_W & 0xFF; buf[5] = (STRIP_W >> 8) & 0xFF;
    buf[6] = 4;              buf[7] = 0;
    for (size_t i = STRIP_HDR_BYTES; i < sizeof buf; i++) buf[i] = 0x23;  // RED, YELLOW

    strip_t s;
    CHECK(strip_parse(buf, sizeof buf, &s));
    CHECK(s.w == STRIP_W && s.h == 4 && s.stride == stride);

    // High nibble is the LEFT pixel. Getting this backwards mirrors the
    // picture, which on an illustration is a bug nobody would call a bug.
    CHECK(strip_at(&s, 0, 0) == STRIP_RED);
    CHECK(strip_at(&s, 1, 0) == STRIP_YELLOW);
    CHECK(strip_at(&s, STRIP_W - 1, 3) == STRIP_YELLOW);

    // Out of range paints paper rather than indexing off the buffer, so a
    // drawing loop that runs one pixel long is a cosmetic bug, not a crash.
    CHECK(strip_at(&s, -1, 0) == STRIP_WHITE);
    CHECK(strip_at(&s, STRIP_W, 0) == STRIP_WHITE);
    CHECK(strip_at(&s, 0, 4) == STRIP_WHITE);
    CHECK(strip_at(&s, 0, -1) == STRIP_WHITE);
    CHECK(strip_at(NULL, 0, 0) == STRIP_WHITE);

    // TRUNCATION IS THE FAILURE THAT MATTERS. A morning fetch that drops
    // halfway through is a valid header over half an image, and drawing it
    // paints garbage across the top of the page for a whole day.
    CHECK(!strip_parse(buf, sizeof buf - 1, &s));
    CHECK(!strip_parse(buf, STRIP_HDR_BYTES, &s));
    CHECK(!strip_parse(buf, 0, &s));

    // Nothing else gets drawn either.
    uint8_t bad[sizeof buf];
    memcpy(bad, buf, sizeof bad);
    bad[0] = 'X';                       CHECK(!strip_parse(bad, sizeof bad, &s));
    memcpy(bad, buf, sizeof bad);
    bad[4] = 0x91; bad[5] = 0x01;       CHECK(!strip_parse(bad, sizeof bad, &s));  // 401, not the panel
    memcpy(bad, buf, sizeof bad);
    bad[6] = 0;                         CHECK(!strip_parse(bad, sizeof bad, &s));  // zero height
    memcpy(bad, buf, sizeof bad);
    bad[6] = STRIP_H_MAX + 1;           CHECK(!strip_parse(bad, sizeof bad, &s));
    CHECK(!strip_parse(NULL, 100, &s));
    CHECK(!strip_parse(buf, sizeof buf, NULL));

    // A rejected parse must leave the caller's strip untouched, so a bad
    // fetch cannot half-overwrite the one already on the page.
    strip_t good = s;
    CHECK(strip_parse(buf, sizeof buf, &good));
    strip_t keep = good;
    CHECK(!strip_parse(buf, 12, &good));
    CHECK(memcmp(&good, &keep, sizeof good) == 0);
    return 0;
}

int main(void)
{
    struct { const char *name; int (*fn)(void); } tests[] = {
        { "dst",             test_dst },
        { "schedule",        test_schedule },
        { "state",           test_state },
        { "state_edges",     test_state_edges },
        { "issue_and_turns", test_issue_and_turns },
        { "siblings",        test_siblings },
        { "weekend_select",  test_weekend_selection },
        { "wake_ring",       test_wake_ring },
        { "wake_participation", test_wake_participation },
        { "kids_empty",      test_kids_empty },
        { "kids_birthday",   test_kids_birthday },
        { "kids_turn",       test_kids_turn },
        { "weather_parse",     test_weather_parse },
        { "weather_icons",     test_weather_icons },
        { "weather_advice",    test_weather_advice },
        { "weather_staleness", test_weather_staleness },
        { "schedule_weekday",  test_schedule_weekday },
        { "weekday_he",        test_weekday_he },
        { "schedule_parse",    test_schedule_parse },
        { "sd_json",           test_sd_json },
        { "daily_layout",      test_daily_layout },
        { "he_text",           test_he_text },
        { "riddle_batch",      test_riddle_batch },
        { "formdata",          test_formdata },
        { "masthead_fits",     test_masthead_fits },
        { "strip",             test_strip },
    };
    for (unsigned i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        if (tests[i].fn()) {
            fprintf(stderr, "\n%s FAILED\n", tests[i].name);
            return 1;
        }
        printf("  ok  %s\n", tests[i].name);
    }
    printf("PASS: %d checks\n", checks);
    return 0;
}
