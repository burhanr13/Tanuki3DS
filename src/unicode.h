#ifndef UNICODE_H
#define UNICODE_H

#include "common.h"

int convert_utf16(char* dst, size_t dstlen, u16* src, size_t srclen);
int convert_to_utf16(u16* dst, size_t dstlen, char* src);

#endif