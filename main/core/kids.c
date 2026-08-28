// Morning Riddle: who the kids are. See kids.h for the no-IDF rule.

#include "kids.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

bool kids_valid(const kids_t *k)
{
    if (!k) return false;
    if (k->count > KIDS_MAX) return false;
    for (int i = 0; i < k->count; i++) {
        if (!k->kid[i].name[0]) return false;               // nameless entry
        if (k->kid[i].birth_month > 12) return false;
        if (k->kid[i].birth_day > 31) return false;
        // A month with no day (or the reverse) is a half-filled record. Allow
        // it -- the name still works for callouts -- but kids_birthday_on()
        // will never match it, which is the safe direction.
    }
    return true;
}

int kids_birthday_on(const kids_t *k, int month, int day)
{
    if (!kids_valid(k) || month < 1 || month > 12 || day < 1 || day > 31)
        return -1;
    for (int i = 0; i < k->count; i++) {
        if (k->kid[i].birth_month == month && k->kid[i].birth_day == day)
            return i;
    }
    return -1;
}

int kids_pick_callout(const kids_t *k, int32_t day)
{
    if (!kids_valid(k) || k->count == 0) return -1;

    // Knuth's multiplicative hash. Cheap, and good enough to keep consecutive
    // days from correlating -- which plain `day % 3` would not, since it would
    // fire on a fixed weekday-like cycle and name the kids in strict rotation.
    uint32_t h = (uint32_t)day * 2654435761u;
    h ^= h >> 16;

    if (h % KIDS_CALLOUT_ONE_IN != 0) return -1;
    return (int)((h / KIDS_CALLOUT_ONE_IN) % k->count);
}

bool kids_parse(const char *json, kids_t *out)
{
    if (!json || !out) return false;

    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "kids");
    if (!cJSON_IsArray(arr)) { cJSON_Delete(root); return false; }

    // Build into a local and commit only on success, so a partly-valid file
    // cannot leave half the family in place over a good stored set.
    kids_t k;
    memset(&k, 0, sizeof k);

    const cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (k.count >= KIDS_MAX) break;
        if (!cJSON_IsObject(it)) continue;

        const cJSON *nm = cJSON_GetObjectItemCaseSensitive(it, "name");
        if (!cJSON_IsString(nm) || !nm->valuestring[0]) continue;

        kid_t *kid = &k.kid[k.count];
        snprintf(kid->name, KID_NAME_MAX, "%s", nm->valuestring);

        // A birthday is optional. Out-of-range values are dropped rather than
        // stored, so kids_birthday_on can never match a 13th month.
        const cJSON *mo = cJSON_GetObjectItemCaseSensitive(it, "month");
        const cJSON *dy = cJSON_GetObjectItemCaseSensitive(it, "day");
        if (cJSON_IsNumber(mo) && cJSON_IsNumber(dy)) {
            const int m = mo->valueint, d = dy->valueint;
            if (m >= 1 && m <= 12 && d >= 1 && d <= 31) {
                kid->birth_month = (uint8_t)m;
                kid->birth_day   = (uint8_t)d;
            }
        }
        k.count++;
    }

    cJSON_Delete(root);
    *out = k;
    return true;
}
