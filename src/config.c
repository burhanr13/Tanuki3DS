#include "config.h"

#include <ctype.h>

#include "arm/jit/jit.h"
#include "common.h"
#include "emulator.h"

#define MAX_LINE 1000

char* lskip(char* p) {
    while (*p && isspace(*p)) p++;
    return p;
}

void rtrim(char* p) {
    char* end = p + strlen(p);
    while (end != p && isspace(end[-1])) *--end = '\0';
}

void load_config() {
    FILE* fp = fopen("ctremu.ini", "r");
    if (!fp) {
        return;
    }

    char section[MAX_LINE] = {};
    char buf[MAX_LINE];
    while (fgets(buf, MAX_LINE, fp)) {
        char* key = lskip(buf);
        if (!*key) continue;
        if (*key == '#') continue;

        if (*key == '[') {
            key++;
            char* end = strchr(key, ']');
            if (!end) continue;
            *end = '\0';
            key = lskip(key);
            rtrim(key);
            strcpy(section, key);
            continue;
        }
        char* val = strchr(key, '=');
        if (!val) continue;

        *val++ = '\0';
        rtrim(key);
        val = lskip(val);
        rtrim(val);

        if (false) {
#define SECT(k)                                                                \
    }                                                                          \
    else if (!strcmp(section, k)) {                                            \
        if (false) {}
#define CMT(k)
#define MATCH(k) else if (!strcmp(key, k))
#define BOOL(k, v) MATCH(k) v = !strcmp(val, "true");
#define INT(k, v) MATCH(k) v = atoi(val);
#define FLT(k, v) MATCH(k) v = atof(val);
#define PSTR(k, v) MATCH(k) free(v), v = val[0] ? strdup(val) : nullptr;
#define ASTR(k, v)                                                             \
    MATCH(k) strncpy(v, val, sizeof v - 1), v[sizeof v - 1] = '\0';
#include "config.inc"
#undef SECT
#undef CMT
#undef MATCH
#undef BOOL
#undef INT
#undef FLT
#undef PSTR
#undef ASTR
        }
    }

    fclose(fp);
}

void save_config() {
    FILE* fp = fopen("ctremu.ini", "w");
    if (!fp) {
        lerror("failed to open config");
        return;
    }

#define SECT(k) fprintf(fp, "\n[" k "]\n");
#define CMT(k, ...) fprintf(fp, "# " k "\n" __VA_OPT__(, ) __VA_ARGS__);
#define BOOL(k, b) fprintf(fp, k " = %s\n", b ? "true" : "false");
#define INT(k, i) fprintf(fp, k " = %d\n", i);
#define FLT(k, f) fprintf(fp, k " = %f\n", f);
#define PSTR(k, s) fprintf(fp, k " = %s\n", s ? s : "");
#define ASTR(k, s) fprintf(fp, k " = %s\n", s);
#include "config.inc"
#undef SECT
#undef CMT
#undef BOOL
#undef INT
#undef FLT
#undef PSTR
#undef ASTR

    fclose(fp);
}
