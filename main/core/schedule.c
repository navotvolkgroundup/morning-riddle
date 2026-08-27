// Morning Riddle: today's school timetable. See schedule.h for the why.

#include "schedule.h"

#include <string.h>

#include "cJSON.h"

// Index order is Sunday-first to match both tm_wday and the Israeli school
// week. The names are what appear in the file, so nothing depends on this
// order except this table.
static const char *kDayKeys[SCHED_DAYS] = {
    "sun", "mon", "tue", "wed", "thu", "fri", "sat"
};

int schedule_weekday(int32_t civil_day)
{
    // +4 because 1970-01-01 was a Thursday and Sunday is 0.
    // The extra +7 before the second modulo keeps pre-epoch days non-negative;
    // C's % returns a negative remainder for negative operands, which would
    // index off the front of the array.
    int32_t w = ((civil_day + 4) % SCHED_DAYS + SCHED_DAYS) % SCHED_DAYS;
    return (int)w;
}

// hebrew.inc puts ink down for the Hebrew block and printable ASCII, and
// silently draws nothing for anything else. A subject with a curly quote or a
// niqqud mark would appear as a hole in the middle of a word, so such strings
// are skipped whole rather than rendered broken.
static bool drawable(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned c = *p;
        if (c == 0x20 || (c >= 0x21 && c < 0x7F)) { p++; continue; }
        if ((c & 0xE0) == 0xC0 && p[1]) {                 // 2-byte UTF-8
            unsigned cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);
            if (cp >= 0x05D0 && cp <= 0x05EA) { p += 2; continue; }
            return false;
        }
        return false;
    }
    return true;
}

// Appends "sep + subject" if the whole thing still fits. Returns false when it
// would not, so the caller can stop rather than emit a half-written subject.
static bool append_subject(char *line, size_t cap, const char *subject)
{
    size_t have = strlen(line);
    size_t sep  = (have == 0) ? 0 : strlen(SCHED_SEP);
    size_t need = have + sep + strlen(subject) + 1;
    if (need > cap) return false;
    if (sep) memcpy(line + have, SCHED_SEP, sep);
    memcpy(line + have + sep, subject, strlen(subject) + 1);
    return true;
}

bool schedule_parse(const char *json, schedule_t *out)
{
    if (!json || !out) return false;

    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    const cJSON *days = cJSON_GetObjectItemCaseSensitive(root, "days");
    if (!cJSON_IsObject(days)) { cJSON_Delete(root); return false; }

    // Build into a local; commit only on success so a partly-valid file cannot
    // leave a half-updated timetable over a good stored one.
    schedule_t s;
    memset(&s, 0, sizeof s);

    for (int d = 0; d < SCHED_DAYS; d++) {
        const cJSON *arr = cJSON_GetObjectItemCaseSensitive(days, kDayKeys[d]);
        if (!cJSON_IsArray(arr)) continue;          // omitted day: empty line

        const cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            if (!cJSON_IsString(it) || !it->valuestring[0]) continue;
            if (!drawable(it->valuestring)) continue;   // would render as a hole
            if (!append_subject(s.line[d], SCHED_LINE_MAX, it->valuestring))
                break;                                  // line is full
        }
    }

    cJSON_Delete(root);
    *out = s;
    return true;
}

const char *schedule_for_day(const schedule_t *s, int32_t civil_day)
{
    if (!s) return "";
    return s->line[schedule_weekday(civil_day)];
}

bool schedule_is_empty(const schedule_t *s)
{
    if (!s) return true;
    for (int d = 0; d < SCHED_DAYS; d++)
        if (s->line[d][0]) return false;
    return true;
}
