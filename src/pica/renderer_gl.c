#include "renderer_gl.h"

#include <3ds.h>
#include <emulator.h>

#include "gpu.h"

const char mainvertsource[] = {
#embed "hostshaders/main.vert"
    , '\0'};
const char mainfragsource[] = {
#embed "hostshaders/main.frag"
    , '\0'};

const char gpuvertsource[] = {
#embed "hostshaders/gpu.vert"
    , '\0'};
const char gpufragsource[] = {
#embed "hostshaders/gpu.frag"
    , '\0'};

void renderer_gl_init(GLState* state, GPU* gpu) {
    state->gpu = gpu;

    auto mainvs = glCreateShader(GL_VERTEX_SHADER);
    auto mainfs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(mainvs, 1, &(const char*) {mainvertsource}, nullptr);
    glShaderSource(mainfs, 1, &(const char*) {mainfragsource}, nullptr);
    glCompileShader(mainvs);
    glCompileShader(mainfs);
    state->main_program = glCreateProgram();
    glAttachShader(state->main_program, mainvs);
    glAttachShader(state->main_program, mainfs);
    glLinkProgram(state->main_program);
    glDeleteShader(mainvs);
    glDeleteShader(mainfs);
    glUseProgram(state->main_program);
    glUniform1i(glGetUniformLocation(state->main_program, "screen"), 0);

    glGenVertexArrays(1, &state->main_vao);
    glBindVertexArray(state->main_vao);

    glGenBuffers(1, &state->main_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, state->main_vbo);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STATIC_DRAW);

    state->gpu_vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(state->gpu_vs, 1, &(const char*) {gpuvertsource}, nullptr);
    glCompileShader(state->gpu_vs);

    state->gpu_uberfs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(state->gpu_uberfs, 1, &(const char*) {gpufragsource},
                   nullptr);
    glCompileShader(state->gpu_uberfs);

    LRU_init(state->progcache);

    glGenBuffers(1, &state->vert_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, state->vert_ubo);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, state->vert_ubo);
    glGenBuffers(1, &state->uber_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, state->uber_ubo);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, state->uber_ubo);
    glGenBuffers(1, &state->frag_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, state->frag_ubo);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, state->frag_ubo);

    glGenVertexArrays(1, &state->gpu_vao);
    glBindVertexArray(state->gpu_vao);

    glGenBuffers(12, state->gpu_vbos);
    glGenBuffers(1, &state->gpu_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state->gpu_ebo);

    // for hw vshaders attributes are setup at run time
    if (!ctremu.hwvshaders) {
        glBindBuffer(GL_ARRAY_BUFFER, state->gpu_vbos[0]);

        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void*) offsetof(Vertex, pos));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void*) offsetof(Vertex, color));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void*) offsetof(Vertex, texcoord0));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void*) offsetof(Vertex, texcoord1));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void*) offsetof(Vertex, texcoord2));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void*) offsetof(Vertex, texcoordw));
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void*) offsetof(Vertex, normquat));
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void*) offsetof(Vertex, view));
        glEnableVertexAttribArray(7);
    }

    glGenTextures(2, state->screentex);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, state->screentex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     SCREEN_HEIGHT * ctremu.videoscale,
                     SCREEN_WIDTH(i) * ctremu.videoscale, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

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

    GLuint textures[TEX_MAX];
    glGenTextures(TEX_MAX, textures);
    for (int i = 0; i < TEX_MAX; i++) {
        gpu->textures.d[i].tex = textures[i];
    }

    glBindVertexArray(state->gpu_vao);
}

void renderer_gl_destroy(GLState* state) {
    glDeleteProgram(state->main_program);
    glDeleteShader(state->gpu_vs);
    glDeleteShader(state->gpu_uberfs);
    for (int i = 0; i < MAX_PROGRAM; i++) {
        glDeleteProgram(state->progcache.d[i].prog);
    }
    for (int i = 0; i < VSH_MAX; i++) {
        glDeleteShader(state->gpu->vshaders_hw.d[i].vs);
    }
    for (int i = 0; i < FSH_MAX; i++) {
        glDeleteShader(state->gpu->fshaders.d[i].fs);
    }
    glDeleteVertexArrays(1, &state->main_vao);
    glDeleteVertexArrays(1, &state->gpu_vao);
    glDeleteBuffers(1, &state->main_vbo);
    glDeleteBuffers(12, state->gpu_vbos);
    glDeleteBuffers(1, &state->uber_ubo);
    glDeleteBuffers(1, &state->frag_ubo);
    glDeleteBuffers(1, &state->gpu_ebo);
    glDeleteTextures(2, state->screentex);
    for (int i = 0; i < FB_MAX; i++) {
        glDeleteFramebuffers(1, &state->gpu->fbs.d[i].fbo);
        glDeleteTextures(1, &state->gpu->fbs.d[i].color_tex);
        glDeleteTextures(1, &state->gpu->fbs.d[i].depth_tex);
    }
    for (int i = 0; i < TEX_MAX; i++) {
        glDeleteTextures(1, &state->gpu->textures.d[i].tex);
    }
}

// call before emulating gpu drawing
void render_gl_setup_gpu(GLState* state) {
    glBindVertexArray(state->gpu_vao);
    glUseProgram(LRU_mru(state->progcache)->prog);
    glBindFramebuffer(GL_FRAMEBUFFER, state->gpu->curfb->fbo);
}

// call at end of frame
// leaves framebuffer 0 bound at the end because on mac
// swap buffers wont work if it is not
void render_gl_main(GLState* state, int view_w, int view_h) {
    // reset gl for drawing the main window
    glUseProgram(state->main_program);
    glBindVertexArray(state->main_vao);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glColorMask(true, true, true, true);
    glDisable(GL_BLEND);
    glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);

    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);

    glViewport(0, view_h / 2, view_w, view_h / 2);
    glBindTexture(GL_TEXTURE_2D, state->screentex[SCREEN_TOP]);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glViewport(view_w * (SCREEN_WIDTH_TOP - SCREEN_WIDTH_BOT) /
                   (2 * SCREEN_WIDTH_TOP),
               0, view_w * SCREEN_WIDTH_BOT / SCREEN_WIDTH_TOP, view_h / 2);
    glBindTexture(GL_TEXTURE_2D, state->screentex[SCREEN_BOT]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void gpu_gl_load_prog(GLState* state, GLuint vs, GLuint fs) {
    if (LRU_mru(state->progcache)->vs == vs &&
        LRU_mru(state->progcache)->fs == fs) {
        return;
    }

    auto ent = LRU_load(state->progcache, vs | ((u64) fs << 32));
    if (ent->vs != vs || ent->fs != fs) {
        glDeleteProgram(ent->prog);
        ent->vs = vs;
        ent->fs = fs;
        ent->prog = glCreateProgram();
        glAttachShader(ent->prog, ent->vs);
        glAttachShader(ent->prog, ent->fs);
        glLinkProgram(ent->prog);
        glUseProgram(ent->prog);
        glUniform1i(glGetUniformLocation(ent->prog, "tex0"), 0);
        glUniform1i(glGetUniformLocation(ent->prog, "tex1"), 1);
        glUniform1i(glGetUniformLocation(ent->prog, "tex2"), 2);
        if (ent->vs != state->gpu_vs)
            glUniformBlockBinding(
                ent->prog, glGetUniformBlockIndex(ent->prog, "VertUniforms"),
                0);
        if (ent->fs == state->gpu_uberfs)
            glUniformBlockBinding(
                ent->prog, glGetUniformBlockIndex(ent->prog, "UberUniforms"),
                1);
        glUniformBlockBinding(
            ent->prog, glGetUniformBlockIndex(ent->prog, "FragUniforms"), 2);

        linfo("linked new program");
    } else {
        glUseProgram(ent->prog);
    }
}