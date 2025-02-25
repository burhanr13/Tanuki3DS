#include "config.h"

#include <ctype.h>

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
        linfo("no config file yet");
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

        const char* matchsect = "";

#define SECT(s)                                                                \
    matchsect = s;                                                             \
    if (false) {}
#define CMT(s)
#define MATCH(s, k) else if (!strcmp(section, s) && !strcmp(key, k))
#define BOOL(s, v) MATCH(matchsect, s) v = !strcmp(val, "true");
#define INT(s, v) MATCH(matchsect, s) v = atoi(val);

#include "config.inc"

#undef SECT
#undef CMT
#undef MATCH
#undef BOOL
#undef INT
    }

    fclose(fp);
}

void save_config() {
    FILE* fp = fopen("ctremu.ini", "w");
    if (!fp) {
        lerror("failed to open config");
        return;
    }
#define SECT(s) fprintf(fp, "[" s "]\n");
#define CMT(s) fprintf(fp, "# " s "\n");
#define BOOL(s, b) fprintf(fp, s " = %s\n", b ? "true" : "false");
#define INT(s, i) fprintf(fp, s " = %d\n", i);

#include "config.inc"

#undef SECT
#undef CMT
#undef BOOL
#undef INT

    fclose(fp);
}