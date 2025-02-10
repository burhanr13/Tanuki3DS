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

typedef struct {
    struct {
        struct {
            int src0;
            int op0;
            int src1;
            int op1;
            int src2;
            int op2;
            int combiner;
            float scale;
        } rgb, a;
    } tev[6];

    int tev_update_rgb;
    int tev_update_alpha;
    int tex2coord;
    int _pad1;

    struct {
        int config;
        int _pad[3];
    } light[8];
    int numlights;

    int alphatest;
    int alphafunc;
} UberUniforms;

typedef struct {
    float tev_color[6][4];
    float tev_buffer_color[4];

    struct {
        float specular0[4];
        float specular1[4];
        float diffuse[4];
        float ambient[4];
        float vec[4];
    } light[8];
    float ambient_color[4];

    float alpharef;
} FragUniforms;

typedef struct _FSHCacheEntry {
    u64 hash;
    int fs;

    struct _FSHCacheEntry *next, *prev;
} FSHCacheEntry;

char* shader_gen_fs(UberUniforms* ubuf);

int shader_gen_get(GPU* gpu, UberUniforms* ubuf);

#endif
