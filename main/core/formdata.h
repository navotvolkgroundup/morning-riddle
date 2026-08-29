// One field out of an HTML form POST body.
//
// IDF-free, same rule as the rest of core/. It lives here rather than inside
// portal.cpp because it is the only genuinely fiddly thing on that path and
// the two bugs it has already had are both silent ones:
//
//   1. AN UNANCHORED SEARCH. A plain strstr for "d1=" also matches the tail of
//      "kd1=". With two fields that never bit; the setup form now has
//      twenty-one, and the failure mode is a box reading another box's value.
//
//   2. A HALF-DECODED CHARACTER. Truncation counts URLENCODED bytes and one
//      Hebrew letter is nine of them (%D7%9E), so a long timetable line gets
//      cut mid-letter. The remaining lead byte is not a character; hebrew.cpp
//      indexes on the decoded codepoint, so it draws the wrong glyph rather
//      than simply stopping early.
//
// Neither is visible from a phone. Both are trivial to pin down here.

#ifndef FORMDATA_H
#define FORMDATA_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Copies the value of `name` from an application/x-www-form-urlencoded body
// into `out`, percent-decoded and NUL-terminated. Returns false when the field
// is absent, leaving `out` untouched.
//
// A value too long for `out` is truncated on a UTF-8 character boundary, so
// what lands is always drawable even when it is short.
bool form_field(const char *body, const char *name, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif // FORMDATA_H
