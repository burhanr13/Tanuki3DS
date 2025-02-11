#ifndef SHADERDEC_H
#define SHADERDEC_H

#include "shader.h"

typedef struct _GPU GPU;

typedef struct {
    fvec4 c[96];
    int i[4][4];
    int b_raw;
} VertUniforms;

typedef struct _VSHCacheEntry {
    union {
        u64 hash;
        u64 key;
    };
    int vs;

    struct _VSHCacheEntry *next, *prev;
} VSHCacheEntry;

int shader_dec_get(GPU* gpu);

char* shader_dec_vs(GPU* gpu);

#endif
