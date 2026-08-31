// Morning Riddle: the daily picture. See strip.h for the format and for why
// the generator makes it rather than the board.

#include "strip.h"

#include <string.h>

bool strip_parse(const uint8_t *buf, size_t len, strip_t *out)
{
    if (!buf || !out) return false;
    if (len < STRIP_HDR_BYTES) return false;
    if (memcmp(buf, STRIP_MAGIC, 4) != 0) return false;

    const uint16_t w = (uint16_t)(buf[4] | ((uint16_t)buf[5] << 8));
    const uint16_t h = (uint16_t)(buf[6] | ((uint16_t)buf[7] << 8));

    // Exactly the panel's width, and a height the layout can actually give up.
    // A zero-height strip is not an empty picture, it is a malformed one.
    if (w != STRIP_W) return false;
    if (h == 0 || h > STRIP_H_MAX) return false;

    const size_t stride = ((size_t)w + 1) / 2;

    // The whole point of this check: a fetch that dropped halfway through
    // arrives as a valid header over half an image.
    if (len - STRIP_HDR_BYTES < stride * h) return false;

    out->w      = w;
    out->h      = h;
    out->pix    = buf + STRIP_HDR_BYTES;
    out->stride = stride;
    return true;
}

uint8_t strip_at(const strip_t *s, int x, int y)
{
    if (!s || !s->pix) return STRIP_WHITE;
    if (x < 0 || y < 0 || x >= (int)s->w || y >= (int)s->h) return STRIP_WHITE;

    const uint8_t byte = s->pix[(size_t)y * s->stride + (size_t)(x >> 1)];
    // High nibble is the LEFT pixel -- the order a hex dump reads in.
    const uint8_t ink = (x & 1) ? (uint8_t)(byte & 0x0F) : (uint8_t)(byte >> 4);
    return ink < STRIP_INK_COUNT ? ink : STRIP_WHITE;
}
