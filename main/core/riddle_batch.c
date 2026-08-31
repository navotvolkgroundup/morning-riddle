// Batch parsing. See riddle_batch.h for the document shape.

#include "riddle_batch.h"

#include <stddef.h>
#include <string.h>

#include "cJSON.h"

// Copies a string field. `required` decides whether its absence kills the
// entry. Returns false when a required field is missing or empty.
static bool take_str(const cJSON *obj, const char *key, char *out, size_t cap,
                     bool required)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(v) || !v->valuestring[0]) return !required;
    // Truncate rather than reject. An over-long question renders clipped,
    // which is visible and survivable; dropping the riddle is not.
    strncpy(out, v->valuestring, cap - 1);
    out[cap - 1] = '\0';
    return true;
}

int riddle_batch_parse(const char *json, riddle_batch_t *out)
{
    if (!json || !out) return -1;
    memset(out, 0, sizeof *out);

    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "riddles");
    if (!cJSON_IsArray(arr)) { cJSON_Delete(root); return -1; }

    const cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (out->count >= RB_MAX) break;
        if (!cJSON_IsObject(it)) { out->skipped++; continue; }

        // The kinds the renderer knows. An unknown type is still skipped
        // rather than drawn as a riddle -- that guard is what lets a future
        // content type ship to the generator before it ships to a device that
        // is awkward to reflash.
        static const char *const kKinds[RK_KIND_COUNT] = {
            "riddle", "joke", "word", "math"
        };
        int kind = -1;
        const cJSON *ty = cJSON_GetObjectItemCaseSensitive(it, "type");
        if (!cJSON_IsString(ty)) {
            kind = RK_RIDDLE;                       // absent type means riddle
        } else {
            for (int i = 0; i < RK_KIND_COUNT; i++)
                if (strcmp(ty->valuestring, kKinds[i]) == 0) { kind = i; break; }
        }
        if (kind < 0) { out->skipped++; continue; }

        riddle_item_t r;
        memset(&r, 0, sizeof r);
        r.kind = (uint8_t)kind;
        if (!take_str(it, "q", r.q, sizeof r.q, true) ||
            !take_str(it, "a", r.a, sizeof r.a, true)) {
            out->skipped++;
            continue;
        }
        take_str(it, "by", r.by, sizeof r.by, false);
        take_str(it, "why", r.why, sizeof r.why, false);

        const cJSON *ch = cJSON_GetObjectItemCaseSensitive(it, "choices");
        if (cJSON_IsArray(ch) && cJSON_GetArraySize(ch) == 3) {
            bool ok = true, holds_answer = false;
            for (int i = 0; i < 3; i++) {
                const cJSON *c = cJSON_GetArrayItem(ch, i);
                if (!cJSON_IsString(c) || !c->valuestring[0]) { ok = false; break; }
                strncpy(r.choices[i], c->valuestring, sizeof r.choices[i] - 1);
                r.choices[i][sizeof r.choices[i] - 1] = '\0';
                if (strcmp(r.choices[i], r.a) == 0) holds_answer = true;
            }
            // CHOICES THAT DO NOT CONTAIN THE ANSWER MAKE THE GAME
            // UNWINNABLE. Fall back to a plain reveal rather than offering a
            // child three wrong options and marking every one of them wrong.
            r.has_choices = ok && holds_answer;
        }

        const cJSON *wk = cJSON_GetObjectItemCaseSensitive(it, "weekend");
        r.weekend = cJSON_IsTrue(wk);

        out->item[out->count++] = r;
    }

    cJSON_Delete(root);
    return out->count;
}
