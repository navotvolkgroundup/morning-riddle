#include "formdata.h"

#include <stdio.h>
#include <string.h>

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Percent-decoding, in place. A password with a space, '&' or '+' in it is
// completely ordinary, and decoding one wrongly stores a subtly incorrect
// password -- which presents as "the network refuses us", a miserable thing to
// debug from a wall.
static void url_decode(char *s)
{
    char *o = s;
    for (char *i = s; *i; i++) {
        if (*i == '+') {
            *o++ = ' ';
        } else if (*i == '%' && i[1] && i[2]) {
            const int hi = hexval(i[1]), lo = hexval(i[2]);
            if (hi >= 0 && lo >= 0) { *o++ = (char)(hi * 16 + lo); i += 2; }
            else                    { *o++ = *i; }
        } else {
            *o++ = *i;
        }
    }
    *o = '\0';
}

// Cuts a trailing partial UTF-8 character. Walks back over continuation bytes
// to the lead, then keeps the character only if all of it is present.
static void trim_partial_utf8(char *s)
{
    size_t len = strlen(s);
    size_t i   = len;
    while (i > 0 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) i--;
    if (i == 0) { if (len) s[0] = '\0'; return; }   // continuations with no lead

    const unsigned char lead = (unsigned char)s[i - 1];
    if ((lead & 0x80) == 0) return;                 // ASCII: nothing pending
    const size_t want = (lead >= 0xF0) ? 4 : (lead >= 0xE0) ? 3 : (lead >= 0xC0) ? 2 : 1;
    if (len - (i - 1) < want) s[i - 1] = '\0';
}

bool form_field(const char *body, const char *name, char *out, size_t out_len)
{
    if (!body || !name || !out || out_len == 0) return false;

    char key[24];
    if (snprintf(key, sizeof key, "%s=", name) >= (int)sizeof key) return false;
    const size_t klen = strlen(key);

    // Anchored: the start of the body, or immediately after an '&'. Without
    // this, one field name is found as the tail of a longer one.
    const char *p = NULL;
    for (const char *c = body; (c = strstr(c, key)) != NULL; c += klen) {
        if (c == body || c[-1] == '&') { p = c; break; }
    }
    if (!p) return false;
    p += klen;

    const char *end = strchr(p, '&');
    const size_t avail = end ? (size_t)(end - p) : strlen(p);
    size_t n = avail;
    if (n >= out_len) {
        n = out_len - 1;
        // Do not cut inside a percent-escape. "%D7%90%D7" truncated to
        // "%D7%90%D" decodes to a letter followed by a literal "%D", and a
        // stray "%D" on a timetable line is not something anyone would guess
        // came from a length limit.
        if (n >= 1 && p[n - 1] == '%')      n -= 1;
        else if (n >= 2 && p[n - 2] == '%') n -= 2;
    }
    memcpy(out, p, n);
    out[n] = '\0';

    url_decode(out);
    trim_partial_utf8(out);
    return true;
}
