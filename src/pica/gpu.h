#ifndef GPU_H
#define GPU_H

#include <pthread.h>
#include <stdalign.h>
#include <stdatomic.h>

#include "../common.h"
#include "../memory.h"
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
        u32 : 1;
        u32 mag_filter : 1;
        u32 min_filter : 1;
        u32 : 1;
        u32 etc1 : 4;
        u32 wrap_t : 2;
        u32 : 2;
        u32 wrap_s : 2;
        u32 : 6;
        u32 shadow : 4;
        u32 mipmapfilter : 1;
        u32 : 3;
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
        u32 : 4;
        u32 a0 : 4;
        u32 a1 : 4;
        u32 a2 : 4;
        u32 : 4;
    } source;
    struct {
        u32 rgb0 : 4;
        u32 rgb1 : 4;
        u32 rgb2 : 4;
        u32 a0 : 4;
        u32 a1 : 4;
        u32 a2 : 4;
        u32 : 8;
    } operand;
    struct {
        u32 rgb : 4;
        u32 : 12;
        u32 a : 4;
        u32 : 12;
    } combiner;
    struct {
        u8 r, g, b, a;
    } color;
    struct {
        u32 rgb : 2;
        u32 : 14;
        u32 a : 2;
        u32 : 14;
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
                struct {
                    u32 cullmode : 2;
                    u32 : 30;
                };
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
                struct {
                    u32 enable;
                    u16 x1, y1, x2, y2;
                } scisssortest;
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
                    u32 : 5;
                    u32 tex3coord : 2;
                    u32 tex3enable : 1;
                    u32 : 2;
                    u32 tex2coord : 1;
                    u32 : 2;
                    u32 clearcache : 1;
                    u32 : 15;
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
                    u32 : 4;
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
                    u32 frag_mode : 8;
                    u32 blend_mode : 1;
                    u32 : 23;
                } color_op;
                struct {
                    u32 rgb_eq : 3;
                    u32 : 5;
                    u32 a_eq : 3;
                    u32 : 5;
                    u32 rgb_src : 4;
                    u32 rgb_dst : 4;
                    u32 a_src : 4;
                    u32 a_dst : 4;
                } blend_func;
                struct {
                    u32 logic_op : 4;
                    u32 : 28;
                };
                struct {
                    u8 r;
                    u8 g;
                    u8 b;
                    u8 a;
                } blend_color;
                struct {
                    u32 enable : 1;
                    u32 : 3;
                    u32 func : 3;
                    u32 : 1;
                    u32 ref : 8;
                    u32 : 16;
                } alpha_test;
                struct {
                    u32 enable : 1;
                    u32 : 3;
                    u32 func : 3;
                    u32 : 1;
                    u32 bufmask : 8;
                    u32 ref : 8;
                    u32 mask : 8;
                } stencil_test;
                struct {
                    u32 fail : 3;
                    u32 : 1;
                    u32 zfail : 3;
                    u32 : 1;
                    u32 zpass : 3;
                    u32 : 1;
                    u32 : 20;
                } stencil_op;
                struct {
                    u32 depthtest : 1;
                    u32 : 3;
                    u32 depthfunc : 3;
                    u32 : 1;
                    u32 red : 1;
                    u32 green : 1;
                    u32 blue : 1;
                    u32 alpha : 1;
                    u32 depth : 1;
                    u32 : 19;
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
                    struct {
                        u16 fmt : 3;
                        u16 : 13;
                    };
                } colorbuf_fmt;
                u32 _118[4];
                u32 depthbuf_loc;
                u32 colorbuf_loc;
                struct {
                    u32 width : 12;
                    u32 height : 12;
                    u32 : 8;
                } dim;
            };
            u32 w[0x40];
        } fb;
        union {
            struct {
                struct {
                    struct {
                        u32 b : 8;
                        u32 : 2;
                        u32 g : 8;
                        u32 : 2;
                        u32 r : 8;
                        u32 : 4;
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
                    u32 b : 8;
                    u32 : 2;
                    u32 g : 8;
                    u32 : 2;
                    u32 r : 8;
                    u32 : 4;
                } ambient;
                u32 _1c1;
                struct {
                    u32 numlights : 3;
                    u32 : 29;
                };
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
                        u64 : 4;
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
                    u32 mode : 2;
                    u32 : 6;
                    u32 : 16;
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
                    u16 entrypoint;
                    u16 entrypointhi;
                };
                u64 permutation;
                u32 outmap_mask;
                u32 _28e;
                u32 codetrans_end;
                struct {
                    u32 floatuniform_idx : 7;
                    u32 : 24;
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
    u32 size;

    struct _TexInfo* next;
    struct _TexInfo* prev;

    u32 tex;
} TexInfo;

#define FB_MAX 8
#define TEX_MAX 128

typedef struct _GPU {

#ifdef FASTMEM
    u8* mem;
#else
    E3DSMemory* mem;
#endif

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

    LRUCache(ShaderJitBlock, VSH_MAX) vshaders;

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
        bool die;

        int base;
        void* attrcfg;
        void* vbuf;

        ShaderJitFunc jitfunc;
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
        u32 : 3;
        u32 incmode : 1;
    };
} GPUCommand;

#define I2F(i)                                                                 \
    (((union {                                                                 \
         u32 _i;                                                               \
         float _f;                                                             \
     }) {i})                                                                   \
         ._f)

#define F2I(f)                                                                 \
    (((union {                                                                 \
         float _f;                                                             \
         u32 _i;                                                               \
     }) {f})                                                                   \
         ._i)

void gpu_vshrunner_init(GPU* gpu);
void gpu_vshrunner_destroy(GPU* gpu);

void gpu_display_transfer(GPU* gpu, u32 paddr, int yoff, bool scalex,
                          bool scaley, int screenid);
void gpu_texture_copy(GPU* gpu, u32 srcpaddr, u32 dstpaddr, u32 size,
                      u32 srcpitch, u32 srcgap, u32 dstpitch, u32 dstgap);
void gpu_clear_fb(GPU* gpu, u32 paddr, u32 color);
void gpu_run_command_list(GPU* gpu, u32 paddr, u32 size);

void gpu_drawarrays(GPU* gpu);
void gpu_drawelements(GPU* gpu);
void gpu_drawimmediate(GPU* gpu);

void gpu_update_gl_state(GPU* gpu);

#endif
