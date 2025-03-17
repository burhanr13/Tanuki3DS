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
#include "shadergen.h"
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
    u32 width, height;
    u32 fmt;
    u32 size;

    struct _TexInfo *next, *prev;

    u32 tex;
} TexInfo;

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

    fvec4 fixattrs[16];
    u32 curfixattr;
    int curfixi;
    Vector(fvec4) immattrs;

    u32 curuniform;
    int curunifi;
    alignas(16) fvec4 floatuniform[96];
    bool uniform_dirty;

    LRUCache(FBInfo, FB_MAX) fbs;
    FBInfo* curfb;
    LRUCache(TexInfo, TEX_MAX) textures;
    LRUCache(ShaderJitBlock, VSH_MAX) vshaders_sw;
    LRUCache(VSHCacheEntry, VSH_MAX) vshaders_hw;
    LRUCache(FSHCacheEntry, FSH_MAX) fshaders;

    struct {
        struct {
            pthread_t thd;

            bool ready;
            int off;
            int count;
        } thread[MAX_VSH_THREADS];

        pthread_cond_t cv1;
        pthread_cond_t cv2;
        pthread_mutex_t mtx;

        atomic_int cur;
        bool die;

        int base;
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

void gpu_init(GPU* gpu);
void gpu_destroy(GPU* gpu);

void gpu_vshrunner_init(GPU* gpu);
void gpu_vshrunner_destroy(GPU* gpu);

void gpu_display_transfer(GPU* gpu, u32 paddr, int yoff, bool scalex,
                          bool scaley, bool vflip, int screenid);
void gpu_render_lcd_fb(GPU* gpu, u32 paddr, u32 fmt, int screenid);
void gpu_texture_copy(GPU* gpu, u32 srcpaddr, u32 dstpaddr, u32 size,
                      u32 srcpitch, u32 srcgap, u32 dstpitch, u32 dstgap);
void gpu_clear_fb(GPU* gpu, u32 paddr, u32 len, u32 value, u32 datasz);
void gpu_run_command_list(GPU* gpu, u32 paddr, u32 size);
void gpu_invalidate_range(GPU* gpu, u32 paddr, u32 len);

void gpu_drawarrays(GPU* gpu);
void gpu_drawelements(GPU* gpu);
void gpu_drawimmediate(GPU* gpu);

#endif
