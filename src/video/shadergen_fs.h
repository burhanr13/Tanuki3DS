#ifndef SHADERGEN_H
#define SHADERGEN_H

#include "common.h"

#include "renderer_gl.h"

#define FSH_MAX 1024

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

enum {
    LLUT_D0,
    LLUT_D1,
    LLUT_SP,
    LLUT_FR,
    LLUT_RB,
    LLUT_RG,
    LLUT_RR,
    LLUT_DA,
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

    union {
        u32 w;
        struct {
            u32 fogmode : 3;
            u32 densitysource : 1;
            u32 : 4;
            u32 update_rgb : 4;
            u32 update_alpha : 4;
            u32 zflip : 1;
            u32 : 15;
        };
    } tev_buffer;
    union {
        u32 w;
        struct {
            u32 tex0enable : 1;
            u32 tex1enable : 1;
            u32 tex2enable : 1;
            u32 : 5;
            u32 tex3coord : 2;
            u32 tex3enable : 1;
            u32 : 2;
            u32 tex2coord : 1;
            u32 : 2;
            u32 clearcache : 1;
            u32 : 15;
        };
    } texconfig;
    int tex0shadow;
    int shadowPerspective;

    struct {
        union {
            u32 w;
            struct {
                u32 directional : 1;
                u32 twosided : 1;
                u32 use_g0 : 1;
                u32 use_g1 : 1;
                u32 : 28;
            };
        } config;
        int _pad[3];
    } light[8];
    int numlights;

    union {
        u32 w;
        struct {
            u32 shadow : 1;
            u32 : 1;
            u32 frPrimary : 1;
            u32 frSecondary : 1;
            u32 lightenv : 4;
            u32 : 4;
            u32 : 4;
            u32 shadowPrimary : 1;
            u32 shadowSecondary : 1;
            u32 shadowInv : 1;
            u32 shadowAlpha : 1;
            u32 : 2;
            u32 bumpTex : 2;
            u32 shadowTex : 2;
            u32 : 1;
            u32 clampHighlights : 1;
            u32 bumpMode : 2;
            u32 noRecalcBumpVec : 1;
            u32 : 1;
        };
    } lconfig0;
    union {
        u32 w;
        struct {
            u32 shadow : 8;
            u32 spotlight : 8;
            u32 luts : 8;
            u32 distattn : 8;
        };
    } lconfig1;
    int llutAbs;
    int llutSel;
    int llutScale;
    int lightPerm;
    int lightDisable;

    int fragOp;

    int alphatest;
    int alphafunc;
} FragConfig;

typedef struct {
    float tev_color[6][4];
    float tev_buffer_color[4];

    struct {
        float specular0[4];
        float specular1[4];
        float diffuse[4];
        float ambient[4];
        float vec[4];
        float spotdir[4];
        float attn_bias;
        float attn_scale;
        float _pad[2];
    } light[8];
    float ambient_color[4];
    float fog_color[4];

    float shadowBias;
    float alpharef;
} FragUniforms;

typedef struct _FSHCacheEntry {
    union {
        u64 hash;
        u64 key;
    };
    int fs;

    struct _FSHCacheEntry *next, *prev;
} FSHCacheEntry;

char* shader_gen_fs(FragConfig* fcfg);

#endif
