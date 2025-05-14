#ifndef RENDERER_GL_H
#define RENDERER_GL_H

#include <glad/glad.h>

#include "common.h"

#define MAX_PROGRAM 1024

typedef struct _GPU GPU;

typedef struct _ProgCacheEntry {
    union {
        struct {
            GLuint vs;
            GLuint fs;
        };
        u64 key;
    };
    GLuint prog;

    struct _ProgCacheEntry *next, *prev;
} ProgCacheEntry;

typedef struct {
    GLuint main_vao;
    GLuint main_vbo;
    GLuint main_program;

    GLuint gpu_vao_sw;
    GLuint gpu_vao_hw;
    GLuint gpu_vbos[12];
    GLuint gpu_ebo;

    GLuint gpu_vs;
    GLuint gpu_uberfs;

    LRUCache(ProgCacheEntry, MAX_PROGRAM) progcache;

    GLuint screentex[2];
    GLuint screenfbo[2];

    GLuint swrendertex;
    GLuint swrenderfbo;

    GLuint blanktex;

    union {
        GLuint ubos[4];
        struct {
            GLuint vert_ubo;
            GLuint uber_ubo;
            GLuint frag_ubo;
            GLuint freecam_ubo;
        };
    };

} GLState;

void renderer_gl_init(GLState* state, GPU* gpu);
void renderer_gl_destroy(GLState* state, GPU* gpu);

void gpu_gl_start_frame(GPU* gpu);
void render_gl_main(GLState* state);
void renderer_gl_update_freecam(GLState* state);

void gpu_gl_display_transfer(GPU* gpu, u32 paddr, int yoff, bool scalex,
                          bool scaley, bool vflip, int screenid);
void gpu_gl_render_lcd_fb(GPU* gpu, u32 paddr, u32 fmt, int screenid);
void gpu_gl_texture_copy(GPU* gpu, u32 srcpaddr, u32 dstpaddr, u32 size,
                      u32 srcpitch, u32 srcgap, u32 dstpitch, u32 dstgap);
void gpu_gl_clear_fb(GPU* gpu, u32 paddr, u32 len, u32 value, u32 datasz);

void gpu_gl_draw(GPU* gpu, bool elements, bool immediate);

#endif
