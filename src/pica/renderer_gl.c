#include "renderer_gl.h"

#include "../3ds.h"
#include "../emulator.h"
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
    state->mainprogram = glCreateProgram();
    glAttachShader(state->mainprogram, mainvs);
    glAttachShader(state->mainprogram, mainfs);
    glLinkProgram(state->mainprogram);
    glDeleteShader(mainvs);
    glDeleteShader(mainfs);
    glUseProgram(state->mainprogram);
    glUniform1i(glGetUniformLocation(state->mainprogram, "screen"), 0);

    glGenVertexArrays(1, &state->mainvao);
    glBindVertexArray(state->mainvao);

    glGenBuffers(1, &state->mainvbo);
    glBindBuffer(GL_ARRAY_BUFFER, state->mainvbo);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STATIC_DRAW);

    state->gpu_vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(state->gpu_vs, 1, &(const char*) {gpuvertsource}, nullptr);
    glCompileShader(state->gpu_vs);

    state->gpu_uberfs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(state->gpu_uberfs, 1, &(const char*) {gpufragsource},
                   nullptr);
    glCompileShader(state->gpu_uberfs);

    LRU_init(state->progcache);
    
    glGenBuffers(1, &state->uber_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, state->uber_ubo);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, state->uber_ubo);

    glGenBuffers(1, &state->frag_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, state->frag_ubo);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, state->frag_ubo);

    glGenVertexArrays(1, &state->gpuvao);
    glBindVertexArray(state->gpuvao);

    glGenBuffers(1, &state->gpuvbo);
    glBindBuffer(GL_ARRAY_BUFFER, state->gpuvbo);
    glGenBuffers(1, &state->gpuebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state->gpuebo);

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
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*) offsetof(Vertex, normquat));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*) offsetof(Vertex, view));
    glEnableVertexAttribArray(6);

    for (int i = 0; i < 2; i++) {
        glGenTextures(1, &state->screentex[i]);
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

    glBindVertexArray(state->gpuvao);
}

void renderer_gl_destroy(GLState* state) {
    glDeleteProgram(state->mainprogram);
    glDeleteShader(state->gpu_vs);
    glDeleteShader(state->gpu_uberfs);
    for (int i = 0; i < MAX_PROGRAM; i++) {
        glDeleteProgram(state->progcache.d[i].prog);
    }
    glDeleteVertexArrays(1, &state->mainvao);
    glDeleteVertexArrays(1, &state->gpuvao);
    glDeleteBuffers(1, &state->mainvbo);
    glDeleteBuffers(1, &state->gpuvbo);
    glDeleteBuffers(1, &state->uber_ubo);
    glDeleteBuffers(1, &state->frag_ubo);
    glDeleteBuffers(1, &state->gpuebo);
    glDeleteTextures(1, &state->screentex[SCREEN_TOP]);
    glDeleteTextures(1, &state->screentex[SCREEN_BOT]);
    for (int i = 0; i < FB_MAX; i++) {
        glDeleteFramebuffers(1, &state->gpu->fbs.d[i].fbo);
        glDeleteTextures(1, &state->gpu->fbs.d[i].color_tex);
        glDeleteTextures(1, &state->gpu->fbs.d[i].depth_tex);
    }
    for (int i = 0; i < TEX_MAX; i++) {
        glDeleteTextures(1, &state->gpu->textures.d[i].tex);
    }
}

void render_gl_main(GLState* state, int view_w, int view_h) {
    glUseProgram(state->mainprogram);
    glBindVertexArray(state->mainvao);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glColorMask(true, true, true, true);
    glDisable(GL_BLEND);
    glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);

#ifdef WIREFRAME
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);

    glViewport(0, view_h / 2, view_w, view_h / 2);
    glBindTexture(GL_TEXTURE_2D, state->screentex[SCREEN_TOP]);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glViewport(view_w * (SCREEN_WIDTH_TOP - SCREEN_WIDTH_BOT) /
                   (2 * SCREEN_WIDTH_TOP),
               0, view_w * SCREEN_WIDTH_BOT / SCREEN_WIDTH_TOP, view_h / 2);
    glBindTexture(GL_TEXTURE_2D, state->screentex[SCREEN_BOT]);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

#ifdef WIREFRAME
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
#endif

    glBindVertexArray(state->gpuvao);
}

void gpu_gl_load_prog(GLState* state, GLuint vs, GLuint fs) {
    GLuint prog;
    ProgCacheEntry* ent = nullptr;
    for (int i = 0; i < MAX_PROGRAM; i++) {
        ent = &state->progcache.d[i];
        if ((ent->vs == vs && ent->fs == fs) ||
            (ent->vs == 0 && ent->fs == 0)) {
            break;
        }
    }
    if (!ent) ent = LRU_eject(state->progcache);
    LRU_use(state->progcache, ent);

    if (ent->vs != vs || ent->fs != fs) {
        glDeleteProgram(ent->prog);
        ent->vs = vs;
        ent->fs = fs;
        ent->prog = glCreateProgram();
        glAttachShader(ent->prog, ent->vs);
        glAttachShader(ent->prog, ent->fs);
        glLinkProgram(ent->prog);
    }

    glUseProgram(ent->prog);
    glUniform1i(glGetUniformLocation(ent->prog, "tex0"), 0);
    glUniform1i(glGetUniformLocation(ent->prog, "tex1"), 1);
    glUniform1i(glGetUniformLocation(ent->prog, "tex2"), 2);
    glUniformBlockBinding(ent->prog,
                          glGetUniformBlockIndex(ent->prog, "UberUniforms"), 0);
    glUniformBlockBinding(ent->prog,
                          glGetUniformBlockIndex(ent->prog, "FragUniforms"), 1);
}