#ifndef GPU_H
#define GPU_H

#include "../common.h"
#include "renderer_gl.h"
#include "shader.h"

#define GPUREG(r) ((offsetof(GPU, io.r) - offsetof(GPU, io)) >> 2)
#define GPUREG_MAX 0x300

#define SCALE 1

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

typedef struct {
    struct {
        u8 r, g, b, a;
    } border;
    u16 height;
    u16 width;
    struct {
        u32 _0 : 1;
        u32 mag_filter : 1;
        u32 min_filter : 1;
        u32 _3 : 1;
        u32 etc1 : 4;
        u32 wrap_t : 4;
        u32 wrap_s : 4;
        u32 _16_20 : 4;
        u32 shadow : 4;
        u32 mipmap : 4;
        u32 type : 4;
    } param;
    u32 lod;
    u32 addr;
} TexUnitRegs;

#pragma pack(push, 1)
typedef union {
    u32 w[GPUREG_MAX];
    struct {
        union {
            u32 w[0x40];
        } misc;
        union {
            struct {
                u32 facecull_config;
                u32 view_w;
                u32 view_invw;
                u32 view_h;
                u32 view_invh;
                u32 _045[8];
                u32 depthmap_scale;
                u32 depthmap_offset;
                u32 sh_outmap_total;
                u8 sh_outmap[7][4];
                u32 _057[0xa];
                u32 earlydepth_func;
                u32 earlydepth_test1;
                u32 earlydepth_clear;
                u32 sh_outattr_mode;
                u32 scissortest_mode;
                u32 scissortest_pos;
                u32 scissortest_dim;
                s16 view_x;
                s16 view_y;
                u32 _069;
                u32 earlydepth_data;
                u32 _06b[2];
                u32 depthmap_enable;
            };
            u32 w[0x40];
        } raster;
        union {
            struct {
                u32 texunit_cfg;
                TexUnitRegs tex0;
                u32 tex0_cubeaddr[5];
                u32 tex0_shadow;
                u32 _08c[2];
                u32 tex0_fmt;
            };
            u32 w[0x80];
        } tex;
        union {
            struct {
                struct {
                    u8 frag_mode;
                    u8 blend_mode;
                    u16 unused;
                } color_op;
                struct {
                    u32 rgb_eq : 8;
                    u32 a_eq : 8;
                    u32 rgb_src : 4;
                    u32 rgb_dst : 4;
                    u32 a_src : 4;
                    u32 a_dst : 4;
                } blend_func;
                u32 logic_op;
                struct {
                    u8 r;
                    u8 g;
                    u8 b;
                    u8 a;
                } blend_color;
                struct {
                    u32 enable : 4;
                    u32 func : 4;
                    u32 ref : 8;
                    u32 _16_31 : 16;
                } alpha_test;
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
                struct {
                    u32 width : 12;
                    u32 height : 12;
                    u32 _24_31 : 8;
                } dim;
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
                u32 _236[2];
                struct {
                    u32 size[2];
                    u32 addr[2];
                    u32 jmp[2];
                } cmdbuf;
                u32 _23e[4];
                u32 vsh_num_attr;
                u32 _243;
                u32 vsh_com_mode;
                u32 start_draw_func0;
                u32 _246[0x18];
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
        } gsh, vsh;
        u32 undoc[0x20];
    };
} GPURegs;
#pragma pack(pop)

typedef struct _FBInfo {
    u32 color_paddr;
    u32 depth_paddr;
    u32 width, height;

    struct _FBInfo* next;
    struct _FBInfo* prev;

    u32 fbo;
    u32 color_tex;
    u32 depth_tex;
} FBInfo;

typedef struct _TexInfo {
    u32 paddr;
    u32 width, height;
    u32 fmt;

    struct _TexInfo* next;
    struct _TexInfo* prev;

    u32 tex;
} TexInfo;

#define FB_MAX 8
#define TEX_MAX 128

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
    Vector(fvec) immattrs;

    u32 curuniform;
    int curunifi;

    fvec cst[96];
    fvec in[16];
    fvec out[16];
    fvec reg[16];
    u32 addr[2];
    u32 loopct;
    bool cmp[2];

    LRUCache(FBInfo, FB_MAX) fbs;
    FBInfo* cur_fb;

    LRUCache(TexInfo, TEX_MAX) textures;

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

void gpu_display_transfer(GPU* gpu, u32 paddr, int yoff, bool top);

void gpu_clear_fb(GPU* gpu, u32 paddr, u32 color);

void gpu_run_command_list(GPU* gpu, u32 paddr, u32 size);

void gpu_drawarrays(GPU* gpu);
void gpu_drawelements(GPU* gpu);
void gpu_drawimmediate(GPU* gpu);

void gpu_update_gl_state(GPU* gpu);

#endif
