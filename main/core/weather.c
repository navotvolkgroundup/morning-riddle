// Morning Riddle: weather for the daily page. See weather.h for the no-IDF rule.

#include "weather.h"

#include <math.h>
#include <string.h>

#include "cJSON.h"

// WMO 4677 present-weather codes, as open-meteo emits them, mapped onto the
// bitmaps already in page_weather/Weather_img. Grouped by what the icon set
// can actually distinguish: there is no drizzle icon, so drizzle borrows light
// rain, and there is one snow icon for every kind of frozen precipitation.
//
//   0        clear                      -> qin
//   1,2      mainly clear/partly cloudy -> duoyun
//   3        overcast                   -> yin
//   45,48    fog, depositing rime fog   -> wumai
//   51,53,55 drizzle                    -> xiaoyu
//   56,57    freezing drizzle           -> xiaoyu
//   61       slight rain                -> xiaoyu
//   63       moderate rain              -> zhongyu
//   65       heavy rain                 -> dayu
//   66,67    freezing rain              -> zhongyu
//   71,73,75 snowfall                   -> xiaxue
//   77       snow grains                -> xiaxue
//   80       slight rain showers        -> xiaoyu
//   81       moderate rain showers      -> zhongyu
//   82       violent rain showers       -> baoyu
//   85,86    snow showers               -> xiaxue
//   95       thunderstorm               -> leiyu
//   96,99    thunderstorm with hail     -> leiyu
//
// Unmapped icons: guafen (wind), shachenbao (sandstorm), yangsha (sand). The
// forecast endpoint used here reports no wind or dust code, so nothing can
// legitimately select them. Adding windspeed to the query would light up
// guafen; that is deliberately not done, because a wind icon competing with
// a rain icon needs a precedence rule nobody has asked for.
const char *wmo_icon(uint16_t code)
{
    switch (code) {
    case 0:                      return "qin";
    case 1: case 2:              return "duoyun";
    case 3:                      return "yin";
    case 45: case 48:            return "wumai";
    case 51: case 53: case 55:
    case 56: case 57:
    case 61: case 80:            return "xiaoyu";
    case 63: case 66: case 67:
    case 81:                     return "zhongyu";
    case 65:                     return "dayu";
    case 82:                     return "baoyu";
    case 71: case 73: case 75:
    case 77: case 85: case 86:   return "xiaxue";
    case 95: case 96: case 99:   return "leiyu";
    // A plausible cloud beats a hole in the page. WMO adds codes over time and
    // this board cannot be reflashed casually, so the default has to be safe
    // rather than loud.
    default:                     return "duoyun";
    }
}

const char *wmo_label(uint16_t code)
{
    switch (code) {
    case 0:                      return "clear";
    case 1: case 2:              return "partly cloudy";
    case 3:                      return "overcast";
    case 45: case 48:            return "fog";
    case 51: case 53: case 55:
    case 56: case 57:            return "drizzle";
    case 61: case 80:            return "light rain";
    case 63: case 81:            return "rain";
    case 65: case 82:            return "heavy rain";
    case 66: case 67:            return "freezing rain";
    case 71: case 73: case 75:
    case 77: case 85: case 86:   return "snow";
    case 95:                     return "thunderstorm";
    case 96: case 99:            return "thunderstorm, hail";
    default:                     return "?";
    }
}

// Tenths of a degree, rounded half away from zero. lround rather than a cast:
// (int)(x*10) truncates, so -3.55 would become -35 instead of -36, and the
// board is going to see negative temperatures in January.
static int16_t to_x10(double celsius)
{
    double v = celsius * 10.0;
    if (v >  32000.0) v =  32000.0;      // clamp before narrowing to int16_t
    if (v < -32000.0) v = -32000.0;
    return (int16_t)lround(v);
}

// First element of a cJSON array, as a number. Returns false if the array is
// missing, empty, or holds something that is not a number -- open-meteo emits
// null in a daily array when a value is unavailable for the range.
static bool first_number(const cJSON *parent, const char *key, double *out)
{
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(parent, key);
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) < 1) return false;
    const cJSON *v = cJSON_GetArrayItem(arr, 0);
    if (!cJSON_IsNumber(v)) return false;
    *out = v->valuedouble;
    return true;
}

bool weather_parse(const char *json, weather_t *out)
{
    if (!json || !out) return false;

    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    // Build into a local and only commit on full success, so a partially
    // valid document cannot leave *out half-updated over a good cached value.
    weather_t w;
    memset(&w, 0, sizeof w);
    bool ok = false;

    const cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
    if (cJSON_IsObject(cur)) {
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(cur, "temperature_2m");
        const cJSON *c = cJSON_GetObjectItemCaseSensitive(cur, "weather_code");
        if (cJSON_IsNumber(t) && cJSON_IsNumber(c)) {
            w.temp_x10 = to_x10(t->valuedouble);
            // Negative or absurd codes would index nothing; clamp into the
            // uint16 the icon lookup expects and let its default catch it.
            double cv = c->valuedouble;
            w.wmo = (cv < 0.0 || cv > 65535.0) ? 65535u : (uint16_t)cv;
            ok = true;
        }
    }

    // The daily block is a bonus. Its absence must not fail the parse -- a
    // current temperature alone is still a usable page.
    const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    if (ok && cJSON_IsObject(daily)) {
        double hi, lo;
        if (first_number(daily, "temperature_2m_max", &hi)) w.hi_x10 = to_x10(hi);
        if (first_number(daily, "temperature_2m_min", &lo)) w.lo_x10 = to_x10(lo);
    }

    cJSON_Delete(root);
    if (ok) *out = w;
    return ok;
}

bool weather_is_stale(const weather_t *w, uint32_t now_utc)
{
    if (!w || w->fetched_at == 0) return true;     // never fetched

    // Backwards clock. This guard does NOT change the result -- the unsigned
    // subtraction below underflows to a huge number and reads as stale on its
    // own, which mutation-testing confirmed by deleting the guard and seeing
    // every test still pass. It stays because depending on wraparound to get
    // the right answer is exactly the kind of implicit cleverness that reads
    // as a bug to the next person. Explicit intent, zero behavioural cost.
    if (now_utc < w->fetched_at) return true;

    return (now_utc - w->fetched_at) > WEATHER_STALE_SECS;
}

// See weather.h for why this exists and why it keys on the current
// temperature. Hebrew is written as UTF-8 escapes to match the rest of the
// codebase; the readable text is in the comment on each line.
const char *weather_advice_he(const weather_t *w)
{
    if (!w) return "";

    // Wet first. The icon set collapses drizzle into light rain and every kind
    // of frozen precipitation into one snow icon; the advice collapses further,
    // because "umbrella" and "raincoat" is the whole decision space a child has.
    switch (w->wmo) {
    case 71: case 73: case 75: case 77: case 85: case 86:
        return "\xd7\x9e\xd7\xa2\xd7\x99\xd7\x9c \xd7\x95\xd7\x9b\xd7\xa4\xd7\xa4\xd7\x95\xd7\xaa"; // coat and gloves
    case 65: case 82: case 95: case 96: case 99:
        return "\xd7\x9e\xd7\xa2\xd7\x99\xd7\x9c \xd7\x92\xd7\xa9\xd7\x9d";                        // raincoat
    case 51: case 53: case 55: case 56: case 57:
    case 61: case 63: case 66: case 67: case 80: case 81:
        return "\xd7\x9e\xd7\x98\xd7\xa8\xd7\x99\xd7\x99\xd7\x94";                                 // umbrella
    default:
        break;
    }

    const int t = w->temp_x10;
    if (t < 100) return "\xd7\x9e\xd7\xa2\xd7\x99\xd7\x9c \xd7\x97\xd7\x9d";                       // warm coat
    if (t < 150) return "\xd7\x9e\xd7\xa2\xd7\x99\xd7\x9c";                                        // coat
    if (t < 190) return "\xd7\xa1\xd7\x95\xd7\x95\xd7\x98\xd7\xa9\xd7\x99\xd7\xa8\xd7\x98";        // sweatshirt
    if (t < 240) return "\xd7\xa9\xd7\xa8\xd7\x95\xd7\x95\xd7\x9c \xd7\x90\xd7\xa8\xd7\x95\xd7\x9a"; // long sleeves

    // Warm already, and the afternoon is worse. hi_x10 is only trusted when it
    // is actually above the current reading: the daily block is optional in the
    // response, and an absent one leaves it zero, which would otherwise read as
    // a freezing afternoon in the middle of a 30C morning.
    // 30C, not 32. Set at 32 first, which left a 31C Israeli afternoon
    // advising short sleeves -- true, and not the useful half. At this end of
    // the scale the shirt is obvious and the hat is the thing they forget.
    if (w->hi_x10 > t && w->hi_x10 >= 300)
        return "\xd7\x9b\xd7\x95\xd7\x91\xd7\xa2 \xd7\x95\xd7\x9e\xd7\x99\xd7\x9d";                // hat and water
    return "\xd7\x97\xd7\x95\xd7\x9c\xd7\xa6\xd7\x94 \xd7\xa7\xd7\xa6\xd7\xa8\xd7\x94";            // short-sleeved shirt
}

// See weather.h. Every branch here mirrors one in weather_advice_he(); the two
// are tested together against the same boundaries so they cannot drift.
weather_tone_e weather_advice_tone(const weather_t *w)
{
    if (!w) return WX_TONE_PLAIN;

    switch (w->wmo) {
    case 71: case 73: case 75: case 77: case 85: case 86:
        return WX_TONE_COLD;                                   // snow
    case 65: case 82: case 95: case 96: case 99:
    case 51: case 53: case 55: case 56: case 57:
    case 61: case 63: case 66: case 67: case 80: case 81:
        return WX_TONE_WET;
    default:
        break;
    }

    const int t = w->temp_x10;
    if (t < 150) return WX_TONE_COLD;                          // coat, warm coat
    if (t < 240) return WX_TONE_PLAIN;                         // sweatshirt, long sleeves
    if (w->hi_x10 > t && w->hi_x10 >= 300) return WX_TONE_HOT;  // hat and water
    return WX_TONE_PLAIN;                                      // short sleeves
}
