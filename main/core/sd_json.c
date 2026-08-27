// Reading a JSON config file off the SD card. See sd_json.h for the why.

#include "sd_json.h"

#include <stdio.h>

sdj_status_e sdj_read(const char *path, char *buf, size_t cap, size_t *out_len)
{
    if (out_len) *out_len = 0;
    if (!path || !buf || cap < 2) return SDJ_IO;
    buf[0] = '\0';

    FILE *f = fopen(path, "r");
    if (!f) return SDJ_ABSENT;                 // no card or no file: normal

    size_t n = fread(buf, 1, cap - 1, f);
    if (n == 0) {
        int bad = ferror(f);
        fclose(f);
        return bad ? SDJ_IO : SDJ_EMPTY;
    }

    // Did it all fit? Ask for one more byte. This is the whole point of the
    // function: the original just kept what fitted, so an oversized file was
    // reported as "did not parse" and the real cause stayed invisible.
    int extra = fgetc(f);
    int bad = ferror(f);
    fclose(f);

    if (extra != EOF) {
        buf[0] = '\0';                         // refuse to hand back a fragment
        return SDJ_TOO_BIG;
    }
    if (bad) { buf[0] = '\0'; return SDJ_IO; }

    buf[n] = '\0';
    if (out_len) *out_len = n;
    return SDJ_OK;
}

const char *sdj_strerror(sdj_status_e s)
{
    switch (s) {
    case SDJ_OK:      return "ok";
    case SDJ_ABSENT:  return "no card or no such file";
    case SDJ_EMPTY:   return "file is empty";
    case SDJ_TOO_BIG: return "file is larger than the buffer";
    case SDJ_IO:      return "read failed";
    default:          return "?";
    }
}
