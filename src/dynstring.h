#ifndef DYNSTRING_H
#define DYNSTRING_H

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
    char* buf;
    va_list v;
    va_start(v);
    int n = vasprintf(&buf, fmt, v);
    va_end(v);
    if (s->cur + n >= s->end) {
        auto curoff = s->cur - s->str;
        auto curlen = s->end - s->str;
        auto newlen = 2 * curlen;
        if (curlen + n >= newlen) newlen = curlen + n + 1;
        s->str = realloc(s->str, newlen);
        s->cur = s->str + curoff;
        s->end = s->str + newlen;
    }
    strcpy(s->cur, buf);
    free(buf);
    s->cur += n;
}

#endif
