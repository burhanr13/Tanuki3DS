#ifndef RENDERER_GL_H
#define RENDERER_GL_H

#include <GL/glew.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <common.h>

#define MAX_PROGRAM 256

typedef struct _GPU GPU;

typedef struct _ProgCacheEntry {
    GLuint vs;
    GLuint fs;
    GLuint prog;

    struct _ProgCacheEntry *next, *prev;
} ProgCacheEntry;

typedef struct {
    GPU* gpu;

    GLuint mainvao;
    GLuint mainvbo;
    GLuint mainprogram;

    GLuint gpuvao;
    GLuint gpuvbo;
    GLuint gpuebo;

    GLuint gpu_vs;
    GLuint gpu_uberfs;

    LRUCache(ProgCacheEntry, MAX_PROGRAM) progcache;
    GLuint curprogram;

    GLuint screentex[2];

    GLuint uber_ubo;
    GLuint frag_ubo;

} GLState;

void renderer_gl_init(GLState* state, GPU* gpu);
void renderer_gl_destroy(GLState* state);

void render_gl_main(GLState* state, int view_w, int view_h);

void gpu_gl_load_prog(GLState* state, GLuint vs, GLuint fs);

#endif
