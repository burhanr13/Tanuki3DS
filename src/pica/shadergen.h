#ifndef SHADERGEN_H
#define SHADERGEN_H

#include <common.h>

#include "renderer_gl.h"

#define FSH_MAX 256

typedef struct _GPU GPU;

enum {
    TEVSRC_COLOR,
    TEVSRC_LIGHT_PRIMARY,
    TEVSRC_LIGHT_SECONDARY,
    TEVSRC_TEX0,
    TEVSRC_TEX1,
    TEVSRC_TEX2,
    TEVSRC_TEX3,

    TEVSRC_BUFFER = 13,
    TEVSRC_CONSTANT,
    TEVSRC_PREVIOUS
};

// light config bits
enum {
    L_DIRECTIONAL = BIT(0),
};

typedef struct _FSHCacheEntry {
    u64 hash;
    int fs;

    struct _FSHCacheEntry *next, *prev;
} FSHCacheEntry;

char* shader_gen_fs(UberUniforms* ubuf);

int shader_gen_get(GPU* gpu, UberUniforms* ubuf);

#endif
