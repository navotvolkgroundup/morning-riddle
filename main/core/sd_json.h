// Reading a JSON config file off the SD card.
//
// IDF-free: only <stdio.h>, so it compiles and is tested on the host. The
// NVS half stays in page_riddle.cc, because that is the part that genuinely
// needs the framework.
//
// WHY THIS EXISTS. kids_import_from_sd() is 41 lines, of which about twenty
// are a generic skeleton -- open, read, parse, compare against what is
// already stored, store if different. schedule.c would have repeated every
// one of them. Two copies of a parse-and-persist path is exactly where one
// gets a fix and the other silently does not, which is the same decay the
// page_common extraction was done to stop. (Eng review D5.)
//
// It also fixes a real wart in the original: `fread(buf, 1, sizeof buf - 1, f)`
// silently keeps whatever fits, so an oversized file surfaced as "did not
// parse" rather than "too big" -- a misleading message on a path someone
// hits eventually, and the same class of bug as the page_news truncation.

#ifndef SD_JSON_H
#define SD_JSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SDJ_OK = 0,
    SDJ_ABSENT,     // no card, or no such file. The NORMAL case, not an error.
    SDJ_EMPTY,      // present but zero bytes
    SDJ_TOO_BIG,    // larger than the buffer; reported honestly, not truncated
    SDJ_IO,         // read failed part-way
} sdj_status_e;

// Reads `path` into `buf` and NUL-terminates it. `out_len` (may be NULL)
// receives the byte count. Never returns a partial file as if it were whole:
// a file that does not fit is SDJ_TOO_BIG and `buf` is left empty, because a
// truncated JSON document parses as garbage or, worse, as something valid.
sdj_status_e sdj_read(const char *path, char *buf, size_t cap, size_t *out_len);

// One-line explanation, for logs and the diagnostics screen.
const char *sdj_strerror(sdj_status_e s);

#ifdef __cplusplus
}
#endif

#endif // SD_JSON_H
