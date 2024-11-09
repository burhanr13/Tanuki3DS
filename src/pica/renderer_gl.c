#include "renderer_gl.h"

#include "../3ds.h"
#include "gpu.h"
#include "hostshaders/hostshaders.h"

GLuint make_shader(const char* vert, const char* frag) {
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vert, NULL);
    glCompileShader(vertexShader);
    int success;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infolog[512];
        glGetShaderInfoLog(vertexShader, 512, NULL, infolog);
        printf("Error compiling vertex shader: %s\n", infolog);
    }

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &frag, NULL);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infolog[512];
        glGetShaderInfoLog(fragmentShader, 512, NULL, infolog);
        printf("Error compiling fragment shader: %s\n", infolog);
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infolog[512];
        glGetProgramInfoLog(program, 512, NULL, infolog);
        printf("Error linking program: %s\n", infolog);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

void renderer_gl_setup(GLState* state, GPU* gpu) {
    state->gpu = gpu;

    state->mainprogram = make_shader(mainvertsource, mainfragsource);
    glUseProgram(state->mainprogram);
    glUniform1i(glGetUniformLocation(state->mainprogram, "screen"), 0);

    glGenVertexArrays(1, &state->mainvao);
    glBindVertexArray(state->mainvao);

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, 0, NULL, GL_STATIC_DRAW);

    state->gpuprogram = make_shader(gpuvertsource, gpufragsource);
    glUseProgram(state->gpuprogram);
    glUniform1i(glGetUniformLocation(state->gpuprogram, "tex0"), 0);
    state->uniforms.tex0enable =
        glGetUniformLocation(state->gpuprogram, "tex0enable");

    glGenVertexArrays(1, &state->gpuvao);
    glBindVertexArray(state->gpuvao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    GLuint ebo;
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*) offsetof(Vertex, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*) offsetof(Vertex, color));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*) offsetof(Vertex, texcoord0));
    glEnableVertexAttribArray(2);

    glGenTextures(1, &state->textop);
    glBindTexture(GL_TEXTURE_2D, state->textop);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenTextures(1, &state->texbot);
    glBindTexture(GL_TEXTURE_2D, state->texbot);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    LRU_init(gpu->fbs);

    GLuint fbos[FB_MAX];
    glGenFramebuffers(FB_MAX, fbos);
    GLuint colorbufs[FB_MAX];
    glGenTextures(FB_MAX, colorbufs);
    GLuint depthbufs[FB_MAX];
    glGenTextures(FB_MAX, depthbufs);

    for (int i = 0; i < FB_MAX; i++) {
        gpu->fbs.d[i].fbo = fbos[i];
        gpu->fbs.d[i].color_tex = colorbufs[i];
        gpu->fbs.d[i].depth_tex = depthbufs[i];
    }

    LRU_init(gpu->textures);

    GLuint textures[TEX_MAX];
    glGenTextures(TEX_MAX, textures);
    for (int i = 0; i < TEX_MAX; i++) {
        gpu->textures.d[i].tex = textures[i];
    }

    glUseProgram(state->gpuprogram);
    glBindVertexArray(state->gpuvao);
}

void render_gl_main(GLState* state) {
    glUseProgram(state->mainprogram);
    glBindVertexArray(state->mainvao);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glColorMask(true, true, true, true);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);

    glViewport(0, SCREEN_HEIGHT * SCALE, SCREEN_WIDTH * SCALE,
               SCREEN_HEIGHT * SCALE);
    glBindTexture(GL_TEXTURE_2D, state->textop);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glViewport((SCREEN_WIDTH - SCREEN_WIDTH_BOT) / 2 * SCALE, 0,
               SCREEN_WIDTH_BOT * SCALE, SCREEN_HEIGHT * SCALE);
    glBindTexture(GL_TEXTURE_2D, state->texbot);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glUseProgram(state->gpuprogram);
    glBindVertexArray(state->gpuvao);
    // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
}
