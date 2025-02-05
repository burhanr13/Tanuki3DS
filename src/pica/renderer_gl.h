#ifndef RENDERER_GL_H
#define RENDERER_GL_H

#include <GL/glew.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "../common.h"

#define MAX_PROGRAM 128

typedef struct _GPU GPU;

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

    GLuint screentex[2];

    GLuint uber_ubo;
    GLuint frag_ubo;

} GLState;

void renderer_gl_init(GLState* state, GPU* gpu);
void renderer_gl_destroy(GLState* state);

void render_gl_main(GLState* state, int view_w, int view_h);

void gpu_gl_load_prog(GLState* state, GLuint vs, GLuint fs);

#endif
