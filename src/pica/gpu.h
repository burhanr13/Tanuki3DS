#ifndef GPU_H
#define GPU_H

#include "../types.h"
#include "renderer_gl.h"
#include "shader.h"

#define GPUREG(r) ((offsetof(GPU, io.r) - offsetof(GPU, io)) >> 2)
#define GPUREG_MAX 0x300
#define GSH sh[0]
#define VSH sh[1]

typedef float fvec[4];
typedef float fvec2[2];

typedef union {
    float semantics[24];
    struct {
        fvec pos;
        fvec normquat;
        fvec color;
        fvec2 texcoord0;
        fvec2 texcoord1;
        float texcoordw;
        float _pad;
        fvec view;
        fvec2 texcoord2;
    };
} Vertex;

#pragma pack(push, 1)
typedef union {
    u32 w[GPUREG_MAX];
    struct {
        union {
            u32 w[0x40];
        } misc;
        union {
            struct {
                u32 _040[16];
                u8 sh_outmap[7][4];
            };
            u32 w[0x40];
        } raster;
        union {
            u32 w[0x80];
        } tex;
        union {
            struct {
                u32 color_op;
                u32 blend_func;
                u32 logic_op;
                u32 blend_color;
                u32 alpha_test;
                u32 stencil_test;
                u32 stencil_op;
                struct {
                    u32 depthtest : 4;
                    u32 depthfunc : 4;
                    u32 red : 1;
                    u32 green : 1;
                    u32 blue : 1;
                    u32 alpha : 1;
                    u32 depth : 1;
                    u32 unused : 19;
                } color_mask;
                u32 _108[8];
                u32 fb_invalidate;
                u32 fb_flush;
                struct {
                    struct {
                        u32 read;
                        u32 write;
                    } colorbuf, depthbuf;
                } perms;
                u32 depthbuf_fmt;
                u32 colorbuf_fmt;
                u32 _118[4];
                u32 depthbuf_loc;
                u32 colorbuf_loc;
            };
            u32 w[0x40];
        } fb;
        union {
            u32 w[0xc0];
        } frag;
        union {
            struct {
                u32 attr_base;
                struct {
                    u64 attr_format : 48;
                    u64 fixed_attr_mask : 12;
                    u64 attr_count : 4;
                };
                struct {
                    u32 offset;
                    struct {
                        u64 comp : 48;
                        u64 size : 8;
                        u64 _unused : 4;
                        u64 count : 4;
                    };
                } attrbuf[12];
                struct {
                    u32 indexbufoff : 31;
                    u32 indexfmt : 1;
                };
                u32 nverts;
                u32 config;
                u32 vtx_off;
                u32 _22b[3];
                u32 drawarrays;
                u32 drawelements;
                u32 _230[2];
                u32 fixattr_idx;
                u32 fixattr_data[3];
                u32 _236[0x28];
                struct {
                    u32 outmapcount : 8;
                    u32 mode : 8;
                    u32 unk : 16;
                } prim_config;
            };
            u32 w[0x80];
        } geom;
        union {
            struct {
                u32 booluniform;
                struct {
                    u8 x;
                    u8 y;
                    u8 z;
                    u8 unused;
                } intuniform[4];
                u32 _285[4];
                u32 inconfig;
                struct {
                    u32 entrypoint : 16;
                    u32 entrypointhi : 16;
                };
                u64 permutation;
                u32 outmap_mask;
                u32 _28e;
                u32 codetrans_end;
                struct {
                    u32 floatuniform_idx : 31;
                    u32 floatuniform_mode : 1;
                };
                u32 floatuniform_data[8];
                u32 _299[2];
                u32 codetrans_idx;
                u32 codetrans_data[8];
                u32 _2a4;
                u32 opdescs_idx;
                u32 opdescs_data[8];
            };
            u32 w[0x30];
        } sh[2];
        u32 undoc[0x20];
    };
} GPURegs;
#pragma pack(pop)

typedef struct {
    u32 paddr;
} FBInfo;

#define FB_MAX 6

typedef struct _GPU {

    u8* mem;

    union {
        u32 progdata[BIT(12)];
        PICAInstr code[BIT(12)];
    };
    u32 opdescs[128];
    u32 sh_idx;

    fvec fixattrs[16];
    u32 curfixattr;
    int curfixi;

    u32 curuniform;
    int curunifi;

    fvec cst[96];
    fvec in[16];
    fvec out[16];
    fvec reg[16];
    u32 addr[2];
    u32 loopct;
    bool cmp[2];

    FBInfo fbs[FB_MAX];
    int cur_fb;

    GLState gl;

    GPURegs io;

} GPU;

typedef union {
    u32 w;
    struct {
        u32 id : 16;
        u32 mask : 4;
        u32 nparams : 8;
        u32 unused : 3;
        u32 incmode : 1;
    };
} GPUCommand;

#define PRINTFVEC(v) printf("[%f,%f,%f,%f] ", (v)[0], (v)[1], (v)[2], (v)[3])

#define I2F(i)                                                                 \
    (((union {                                                                 \
         u32 _i;                                                               \
         float _f;                                                             \
     }){i})                                                                    \
         ._f)

#define F2I(f)                                                                 \
    (((union {                                                                 \
         float _f;                                                             \
         u32 _i;                                                               \
     }){f})                                                                    \
         ._i)

u32 f24tof32(u32 i);
u32 f31tof32(u32 i);

void gpu_set_fb_cur(GPU* gpu, u32 paddr);

void gpu_set_fb_top(GPU* gpu, u32 paddr);
void gpu_set_fb_bot(GPU* gpu, u32 paddr);

void gpu_clear_fb(GPU* gpu, u32 color);

void gpu_run_command_list(GPU* gpu, u32* cmds, u32 size);

void gpu_drawarrays(GPU* gpu);
void gpu_drawelements(GPU* gpu);

#endif
