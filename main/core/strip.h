// Morning Riddle: the daily picture.
//
// THIS FILE AND strip.c MUST NOT INCLUDE ANY ESP-IDF HEADER, same rule as
// riddle_decide.c and weather.c. Parsing a blob that arrived over the network
// is exactly the kind of thing that should be exercised on a host with
// truncated, oversized and lying inputs rather than discovered on a wall.
//
// WHY A PICTURE AT ALL. Google's Glanceboard generates a daily illustration
// from the family calendar and renders it to e-ink, and it is the single
// biggest visual upgrade available to a page like this -- the panel is a
// six-colour Spectra 6 and the page has been using two of them.
//
// WHY NOT ON THE DEVICE. Generating an image needs a model, and a model needs
// a server, an account and something to keep running. The batch is already a
// static JSON on a GitHub release: no server, no subscription, months on a
// battery. So the generator makes the picture at the same time it makes the
// riddles, publishes it as a sibling release asset, and the board fetches and
// blits. The pipeline moves; the device stays dumb.
//
// THE FORMAT, and why it is this and not PNG. A PNG decoder is a dependency, a
// heap profile and a class of parser bugs, for a file this board renders once a
// day onto a panel that can show six colours. Four bits per pixel indexed into
// a fixed palette needs eleven lines to decode and cannot fail in an
// interesting way.
//
//   offset  size  meaning
//   0       4     magic "MRI1"
//   4       2     width,  little-endian
//   6       2     height, little-endian
//   8       ...   ((width + 1) / 2) * height bytes; two pixels per byte,
//                 HIGH nibble first, each nibble a strip_ink_e
//
// The high nibble is the LEFT pixel, which is the order a human reading a hex
// dump expects and the order the generator writes.

#ifndef STRIP_H
#define STRIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The panel's whole gamut. Values are the wire format; the UI maps them to
// M5GFX colours, so nothing here needs a graphics header.
typedef enum {
    STRIP_BLACK = 0,
    STRIP_WHITE = 1,
    STRIP_RED   = 2,
    STRIP_YELLOW= 3,
    STRIP_BLUE  = 4,
    STRIP_GREEN = 5,
    STRIP_INK_COUNT
} strip_ink_e;

#define STRIP_MAGIC     "MRI1"
#define STRIP_HDR_BYTES 8

// Bounds the page is willing to draw. Width is the panel's, exactly: a strip
// that is not full width would need an alignment rule nobody has asked for.
//
// 56 IS THE SIZE, NOT A MAXIMUM, and the page has argued it down twice.
//
// The band shares one slot with the fact of the day -- both are the page's
// second item, and a page with room for two does not get three. That slot is
// what the riddle can spare above the folio: at 56px the zone is 231 against a
// floor of 224, and at 64 it is 223 and the riddle starts clipping.
//
// So a taller band is not a bigger picture, it is a picture that never gets
// drawn. The first cut was 140px and the layout declined it on every day
// except an empty one. A fixed height is also simply better design: the band
// is a constant on the page rather than a thing that moves everything else.
//
// On a birthday and on the afternoon edition there is no band at any height.
// Both mornings already have a second item -- a red banner, or the answer and
// its reason -- and a third is one too many.
#define STRIP_W       400
#define STRIP_H_MAX    56

typedef struct {
    uint16_t       w, h;
    const uint8_t *pix;      // points INTO the caller's buffer; not owned
    size_t         stride;   // bytes per row
} strip_t;

// Validates a fetched blob and fills `out`. Returns false, leaving *out
// untouched, for anything that is not a strip this page can draw: wrong magic,
// a header that does not fit, dimensions outside the caps, or a buffer shorter
// than the pixels the header promises.
//
// TRUNCATION IS THE FAILURE THAT MATTERS. A morning fetch that drops halfway
// through arrives as a valid header over half an image, and drawing it would
// paint garbage across the top of the page for a day. The length check is what
// makes that a skipped picture instead.
bool strip_parse(const uint8_t *buf, size_t len, strip_t *out);

// The ink at (x, y). Out-of-range reads return STRIP_WHITE rather than
// indexing, so a drawing loop that runs one pixel long paints paper.
uint8_t strip_at(const strip_t *s, int x, int y);

#ifdef __cplusplus
}
#endif

#endif // STRIP_H
