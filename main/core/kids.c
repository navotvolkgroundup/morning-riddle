// Morning Riddle: who the kids are. See kids.h for the no-IDF rule.

#include "kids.h"

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
