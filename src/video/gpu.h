#ifndef GPU_H
#define GPU_H

#include <pthread.h>
#include <stdatomic.h>

#include "common.h"
#include "kernel/memory.h"

#include "gpuregs.h"
#include "renderer_gl.h"
#include "shader.h"
#include "shaderdec.h"
#include "shadergen_fs.h"
#include "shaderjit/shaderjit.h"

#define MAX_VSH_THREADS 16

typedef union {
    float semantics[24];
    struct {
        fvec4 pos;
        fvec4 normquat;
        fvec4 color;
        fvec2 texcoord0;
        fvec2 texcoord1;
        float texcoordw;
        float _pad;
        fvec4 view;
        fvec2 texcoord2;
    };
} Vertex;

#define FB_MAX 16
#define TEX_MAX 256

typedef struct _FBInfo {
    union {
        u64 color_paddr;
        u64 key;
    };
    u32 depth_paddr;
    u32 width, height;
    u32 color_fmt;
    u32 color_Bpp;

    bool dirty;

    struct _FBInfo *next, *prev;

    u32 fbo;
    u32 color_tex;
    u32 depth_tex;
} FBInfo;

typedef struct _TexInfo {
    union {
        u64 paddr;
        u64 key;
    };
    u16 width, height;
    u16 fmt;
    u32 size;
    u16 minlod, maxlod;

    u64 hash;
    bool needs_rehash;

    struct _TexInfo *next, *prev;

    u32 tex;
} TexInfo;

typedef struct _GPU {

#ifdef FASTMEM
    u8* mem;
#else
    E3DSMemory* mem;
#endif

    struct {
        u32 progdata[SHADER_CODE_SIZE];
        u32 opdescs[SHADER_OPDESC_SIZE];
        u32 code_idx;
        bool code_dirty;

        u32 curuniform;
        int curunifi;
        alignas(16) fvec4 floatuniform[96];
    } gsh, vsh;

    bool vsh_uniform_dirty; // for hw vertex shaders

    fvec4 fixattrs[16];
    u32 curfixattr;
    int curfixi;
    Vector(fvec4) immattrs;

    LRUCache(FBInfo, FB_MAX) fbs;
    FBInfo* curfb;
    LRUCache(TexInfo, TEX_MAX) textures;
    LRUCache(ShaderJitBlock, VSH_MAX) vshaders_sw;
    LRUCache(VSHCacheEntry, VSH_MAX) vshaders_hw;
    LRUCache(FSHCacheEntry, FSH_MAX) fshaders;

    struct {
        pthread_t threads[MAX_VSH_THREADS];
        volatile atomic_bool ready[MAX_VSH_THREADS];

        volatile atomic_int cur;
        bool die;

        int base;
        int count;
        void* attrcfg;
        void* vbuf;

        ShaderJitFunc shaderfunc;
    } vsh_runner;

    GLState gl;

    GPURegs regs;

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

#define CONVERTFLOAT(e, m, i)                                                  \
    ({                                                                         \
        u32 sgn = (i >> (e + m)) & 1;                                          \
        u32 exp = (i >> m) & MASK(e);                                          \
        u32 mantissa = i & MASK(m);                                            \
        if (exp == 0 && mantissa == 0) {                                       \
            exp = 0;                                                           \
        } else if (exp == MASK(e)) {                                           \
            exp = 0xff;                                                        \
        } else {                                                               \
            exp += BIT(7) - BIT(e - 1);                                        \
        }                                                                      \
        mantissa <<= 23 - m;                                                   \
        I2F(sgn << 31 | exp << 23 | mantissa);                                 \
    })

static inline float cvtf24(u32 i) {
    return CONVERTFLOAT(7, 16, i);
}

static inline float cvtf16(u32 i) {
    return CONVERTFLOAT(5, 10, i);
}

void gpu_init(GPU* gpu);
void gpu_destroy(GPU* gpu);

void gpu_vshrunner_init(GPU* gpu);
void gpu_vshrunner_destroy(GPU* gpu);

void gpu_reset_needs_rehesh(GPU* gpu);
void gpu_run_command_list(GPU* gpu, u32 paddr, u32 size);
void gpu_invalidate_range(GPU* gpu, u32 paddr, u32 len);

void gpu_display_transfer(GPU* gpu, u32 paddr, int yoff, bool scalex,
                          bool scaley, bool vflip, int screenid);
void gpu_render_lcd_fb(GPU* gpu, u32 paddr, u32 fmt, int screenid);
void gpu_texture_copy(GPU* gpu, u32 srcpaddr, u32 dstpaddr, u32 size,
                      u32 srcpitch, u32 srcgap, u32 dstpitch, u32 dstgap);
void gpu_clear_fb(GPU* gpu, u32 paddr, u32 len, u32 value, u32 datasz);

void gpu_draw(GPU* gpu, bool elements, bool immediate);

FBInfo* gpu_fbcache_find_within(GPU* gpu, u32 color_paddr);
TexInfo* gpu_texcache_find_within(GPU* gpu, u32 paddr);

void gpu_init_vsh(GPU* gpu, ShaderUnit* shu);
void gpu_init_gsh(GPU* gpu, ShaderUnit* shu);

void gpu_run_vsh(GPU* gpu, bool immediate, int basevert, int nbufverts,
                 fvec4 (*vshout)[16]);
void gpu_run_gsh(GPU* gpu, ShaderUnit* gsh, bool elements, int basevert,
                 int nverts, fvec4 (*vshout)[16], bool indexsize,
                 void* indexbuf);

void gpu_write_outmap_vtx(GPU* gpu, Vertex* dst, fvec4* src);

u32 morton_swizzle(u32 w, u32 x, u32 y);

#endif
