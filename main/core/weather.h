// Morning Riddle: weather for the daily page.
//
// THIS FILE AND weather.c MUST NOT INCLUDE ANY ESP-IDF HEADER, same rule as
// riddle_decide.c, wake_log.c and kids.c. cJSON is fine -- it is a vendored
// library, not IDF, and it compiles on the host, so the PARSING is testable
// too rather than just the arithmetic around it.
//
// WHY NOT THE VENDOR'S WEATHER CODE. page_weather calls
// t.weather.sojson.com with China Meteorological Administration city codes,
// and the city list that ships on the SD card is 447 entries, every one a
// Chinese province. It cannot serve a non-Chinese location at all -- this is
// structural, not a configuration problem. (Eng review D1.)
//
// open-meteo replaces it: no API key, worldwide, HTTPS on a Let's Encrypt
// cert the ESP-IDF bundle already trusts, and 601 bytes on the wire against
// sojson's 4003. It returns WMO weather codes, which are a documented
// standard rather than a vendor's Chinese condition strings.
//
//   https://api.open-meteo.com/v1/forecast?latitude=..&longitude=..
//     &current=temperature_2m,weather_code
//     &daily=temperature_2m_max,temperature_2m_min,weather_code
//     &timezone=Asia/Jerusalem&forecast_days=1

#ifndef WEATHER_H
#define WEATHER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Temperatures are tenths of a degree as integers, not floats. NVS blobs stay
// trivially comparable, there is no float formatting on the draw path, and
// 0.1C is finer than anything a wall display should claim.
typedef struct {
    int16_t  temp_x10;      // current, e.g. 27.0C -> 270
    int16_t  hi_x10;        // today's forecast high
    int16_t  lo_x10;        // today's forecast low
    uint16_t wmo;           // raw WMO code, kept for the wake log
    uint32_t fetched_at;    // unix seconds UTC; 0 means "never fetched"
} weather_t;

// Beyond this age the page marks the reading stale rather than hiding it.
//
// 18 hours is chosen against the two real intervals: the afternoon wake reads
// the morning's fetch about 6.5 hours later and must NOT call that stale, and
// a fetch that fails the next morning leaves a value 24 hours old which MUST
// be called stale. Anything between those two works; 18 sits clear of both.
#define WEATHER_STALE_SECS (18 * 3600)

// Filename stem of the icon for a WMO code, e.g. "qin". Names match the
// bitmaps already shipping in page_weather/Weather_img, so this adds no new
// artwork. Returns "duoyun" for anything unrecognised -- a plausible cloud is
// a better wrong answer than a blank hole in the page.
const char *wmo_icon(uint16_t code);

// Short English label for the same code, for the wake log and diagnostics.
const char *wmo_label(uint16_t code);

// What to wear today, in Hebrew, for a child getting dressed for school.
//
// A NUMBER IS NOT ADVICE. "19.4C partly cloudy" asks a seven-year-old to know
// what nineteen degrees feels like on the way to school, which is a thing
// adults learn slowly and children do not know at all. The panel already has
// the number; this turns it into the one decision they actually have to make.
//
// KEYED ON THE CURRENT TEMPERATURE, not the day's high, because the morning
// fetch happens at 06:30 and what matters is the air they walk out into. A
// January morning at 10C under an 18C afternoon needs a coat; advising from
// the high would send them out cold. The high is consulted only to add a hat
// and water to an already-warm morning, which is the one case where the
// afternoon is the hazard.
//
// PRECIPITATION OUTRANKS TEMPERATURE. Being wet is worse than being slightly
// wrongly dressed, and a child who reads "t-shirt" on a rainy morning learns
// to stop reading the panel.
//
// Returns a static string, never NULL. Kept SHORT on purpose -- it shares one
// 356px line with the temperature, and a line that overflows is elided with
// "..." exactly where the advice is.
const char *weather_advice_he(const weather_t *w);

// Parses an open-meteo forecast response. Fills everything except
// fetched_at, which the caller stamps because only it knows the clock.
// Returns false if the document is unusable; *out is untouched on failure so
// a bad response cannot clobber a good cached reading.
bool weather_parse(const char *json, weather_t *out);

// True if `w` is too old to present as current, or was never fetched.
bool weather_is_stale(const weather_t *w, uint32_t now_utc);

#ifdef __cplusplus
}
#endif

#endif // WEATHER_H
