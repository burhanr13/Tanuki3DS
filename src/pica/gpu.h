#ifndef GPU_H
#define GPU_H

#include <stdalign.h>
#include <stdatomic.h>
#include <pthread.h>

#include "../common.h"
#include "renderer_gl.h"
#include "shader.h"
#include "shaderjit/shaderjit.h"

#define GPUREG(r) ((offsetof(GPU, io.r) - offsetof(GPU, io)) >> 2)
#define GPUREG_MAX 0x300

#define VSH_THREADS 4

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
        u32 mipmapfilter : 4;
        u32 type : 4;
    } param;
    struct {
        u16 bias;
        u8 max;
        u8 min;
    } lod;
    u32 addr;
} TexUnitRegs;

typedef struct {
    struct {
        u32 rgb0 : 4;
        u32 rgb1 : 4;
        u32 rgb2 : 4;
        u32 _12_15 : 4;
        u32 a0 : 4;
        u32 a1 : 4;
        u32 a2 : 4;
        u32 _28_31 : 4;
    } source;
    struct {
        u32 rgb0 : 4;
        u32 rgb1 : 4;
        u32 rgb2 : 4;
        u32 a0 : 4;
        u32 a1 : 4;
        u32 a2 : 4;
        u32 _24_31 : 8;
    } operand;
    struct {
        u16 rgb;
        u16 a;
    } combiner;
    struct {
        u8 r, g, b, a;
    } color;
    struct {
        u16 rgb;
        u16 a;
    } scale;
    struct {
        u8 r, g, b, a;
    } buffer_color;
    u32 _pad[2];
} TexEnvRegs;

// we need this since there are some 64 bit io regs not aligned to 8 bytes
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
                struct {
                    u32 tex0enable : 1;
                    u32 tex1enable : 1;
                    u32 tex2enable : 1;
                    u32 _3_7 : 5;
                    u32 tex3coord : 2;
                    u32 tex3enable : 1;
                    u32 _11_12 : 2;
                    u32 tex2coord : 1;
                    u32 _14_15 : 2;
                    u32 clearcache : 1;
                    u32 _17_31 : 15;
                } config;
                TexUnitRegs tex0;
                u32 tex0_cubeaddr[5];
                u32 tex0_shadow;
                u32 _08c[2];
                u32 tex0_fmt;
                u32 lighting_enable;
                u32 _090;
                TexUnitRegs tex1;
                u32 tex1_fmt;
                u32 _097[2];
                TexUnitRegs tex2;
                u32 tex2_fmt;
                u32 _09f[0x21];
                TexEnvRegs tev0;
                TexEnvRegs tev1;
                TexEnvRegs tev2;
                TexEnvRegs tev3;
                struct {
                    u32 fogmode : 3;
                    u32 densitysource : 1;
                    u32 _4_7 : 4;
                    u32 update_rgb : 4;
                    u32 update_alpha : 4;
                    u32 zflip : 16;
                } tev_buffer;
                u32 _0e0[0xf];
                TexEnvRegs tev4;
                TexEnvRegs tev5;
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
                struct {
                    u32 enable : 4;
                    u32 func : 4;
                    u32 bufmask : 8;
                    u32 ref : 8;
                    u32 mask : 8;
                } stencil_test;
                struct {
                    u32 fail : 4;
                    u32 zfail : 4;
                    u32 zpass : 4;
                    u32 unused : 20;
                } stencil_op;
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
                struct {
                    u16 size;
                    u16 fmt;
                } colorbuf_fmt;
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
            struct {
                struct {
                    struct {
                        u32 b : 10;
                        u32 g : 10;
                        u32 r : 12;
                    } specular0, specular1, diffuse, ambient;
                    struct {
                        u16 x, y, z, _w;
                    } vec;
                    struct {
                        u16 x, y, z, _w;
                    } spotdir;
                    u32 _8;
                    u32 config;
                    u32 attn_bias;
                    u32 attn_scale;
                    u32 _c[4];
                } light[8];
                struct {
                    u32 b : 10;
                    u32 g : 10;
                    u32 r : 12;
                } ambient;
                u32 _1c1;
                u32 numlights;
            };
            u32 w[0xc0];
        } lighting;
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
                u32 restart_primitive;
            };
            u32 w[0x80];
        } geom;
        union {
            struct {
                u32 booluniform;
                u8 intuniform[4][4];
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
    u32 color_fmt;
    u32 color_Bpp;

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

    u32 progdata[SHADER_CODE_SIZE];
    u32 opdescs[SHADER_OPDESC_SIZE];
    u32 sh_idx;
    bool sh_dirty;

    fvec fixattrs[16];
    u32 curfixattr;
    int curfixi;
    Vector(fvec) immattrs;

    u32 curuniform;
    int curunifi;
    alignas(16) fvec floatuniform[96];

    LRUCache(FBInfo, FB_MAX) fbs;
    FBInfo* cur_fb;

    LRUCache(TexInfo, TEX_MAX) textures;

    ShaderJitBlock jitblock;

    struct {
        struct {
            pthread_t thd;

            bool ready;
            int off;
            int count;
        } thread[VSH_THREADS];

        pthread_cond_t cv1;
        pthread_cond_t cv2;
        pthread_mutex_t mtx;

        atomic_int cur;

        int base;
        void* attrcfg;
        void* vbuf;
    } vsh_runner;

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

extern int g_upscale;

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

void gpu_thrds_init(GPU* gpu);

void gpu_display_transfer(GPU* gpu, u32 paddr, int yoff, bool scalex,
                          bool scaley, bool top);
void gpu_clear_fb(GPU* gpu, u32 paddr, u32 color);
void gpu_run_command_list(GPU* gpu, u32 paddr, u32 size);

void gpu_drawarrays(GPU* gpu);
void gpu_drawelements(GPU* gpu);
void gpu_drawimmediate(GPU* gpu);

void gpu_update_gl_state(GPU* gpu);

#endif
