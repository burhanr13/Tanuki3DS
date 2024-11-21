#ifndef RENDERER_GL_H
#define RENDERER_GL_H

#include <GL/glew.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

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
        float color[4];
    } tev[6];
    float tev_buffer_color[4];

    int tev_update_rgb;
    int tev_update_alpha;
    int tex2coord;
    int _pad1;

    struct {
        float specular0[4];
        float specular1[4];
        float diffuse[4];
        float ambient[4];
        float vec[4];
        int config;
        int _pad[3];
    } light[8];
    float ambient_color[4];
    int numlights;

    int alphatest;
    int alphafunc;
    float alpharef;
} UberUniforms;

typedef struct {
    GPU* gpu;

    GLuint mainvao;
    GLuint mainvbo;
    GLuint mainprogram;
    GLuint gpuvao;
    GLuint gpuvbo;
    GLuint gpuebo;
    GLuint gpuprogram;

    GLuint fbotop;
    GLuint textop;
    GLuint fbobot;
    GLuint texbot;

    GLuint ubo;

} GLState;

void renderer_gl_setup(GLState* state, GPU* gpu);

void render_gl_main(GLState* state);

#endif
