#ifndef RENDERER_GL_H
#define RENDERER_GL_H

#include <GL/glew.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

typedef struct {
    struct {
        GLuint vao;
        GLuint program;
    } main, gpu;

    struct {
        GLuint fbo;
        GLuint tex_colorbuf;
        GLuint tex_depthbuf;
    } fbs[2];
    int fb_top;
    int fb_bot;
} GLState;

void renderer_gl_setup(GLState* state);

void render_gl_main(GLState* state);

#endif
