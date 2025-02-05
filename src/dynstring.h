#ifndef DYNSTRING_H
#define DYNSTRING_H

#include "common.h"

typedef struct {
    char* str;
    char* cur;
    char* end;
} DynString;

static inline void ds_init(DynString* s, size_t len) {
    s->str = malloc(len);
    s->cur = s->str;
    s->end = s->str + len;

    *s->cur = '\0';
}

static inline void ds_free(DynString* s) {
    free(s->str);
    *s = (DynString) {};
}

static inline void ds_printf(DynString* s, const char* fmt, ...) {
    va_list v;
    while (true) {
        int rem = s->end - s->cur;
        va_start(v);
        int n = vsnprintf(s->cur, rem, fmt, v);
        va_end(v);

        if (n < rem) {
            s->cur += n;
            return;
        }

        auto curoff = s->cur - s->str;
        auto curlen = s->end - s->str;
        s->str = realloc(s->str, 2 * curlen);
        s->cur = s->str + curoff;
        s->end = s->str + 2 * curlen;
    }
}

#endif