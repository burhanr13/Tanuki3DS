#include "unicode.h"

// lengths are size of buffer, including null terminator
int convert_utf16(char* dst, size_t dstlen, u16* src, size_t srclen) {
    int dsti = 0;
    for (int i = 0; i < srclen; i++) {
        u16 c = src[i];
        if (c < BIT(7)) {
            if (dsti + 1 > dstlen) break;
            dst[dsti++] = c;
        } else if (c < BIT(11)) {
            if (dsti + 2 > dstlen) break;
            dst[dsti++] = 0xc0 | (c >> 6);
            dst[dsti++] = 0x80 | (c & MASK(6));
        } else {
            if (dsti + 3 > dstlen) break;
            dst[dsti++] = 0xe0 | (c >> 12);
            dst[dsti++] = 0x80 | (c >> 6 & MASK(6));
            dst[dsti++] = 0x80 | (c & MASK(6));
        }
    }
    dst[dsti] = '\0';
    return dsti;
}

int convert_to_utf16(u16* dst, size_t dstlen, char* src) {
    int dsti = 0;
    for (char* p = src; *p && dsti < dstlen - 1; p++) {
        if ((*p & 0x80) == 0) {
            dst[dsti++] = *p;
        } else if ((*p & 0xe0) == 0xc0) {
            if (!(p[1] & 0x80)) break;
            dst[dsti++] = (p[1] & MASK(6)) | (*p & MASK(5)) << 6;
        } else if ((*p & 0xf0) == 0xe0) {
            if (!(p[1] & 0x80 && p[2] & 0x80)) break;
            dst[dsti++] =
                (p[2] & MASK(6)) | (p[1] & MASK(6)) << 6 | (*p & MASK(4)) << 11;
        }
    }
    dst[dsti] = 0;
    return dsti;
}