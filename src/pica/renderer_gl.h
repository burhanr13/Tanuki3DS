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
    GPU* gpu;

    GLuint mainvao;
    GLuint mainprogram;
    GLuint gpuvao;
    GLuint gpuprogram;

    GLuint textop;
    GLuint texbot;

    struct {
        GLint tex0enable;
        GLint alphatest;
        GLint alphafunc;
        GLint alpharef;
    } uniformlocs;

} GLState;

void renderer_gl_setup(GLState* state, GPU* gpu);

void render_gl_main(GLState* state);

#endif
