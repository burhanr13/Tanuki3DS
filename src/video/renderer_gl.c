#include "renderer_gl.h"

#include "3ds.h"
#include "emulator.h"

#include "etc1.h"
#include "gpu.h"
#include "gpu_hash.h"

#include "gpuptr.inc"

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

    glGenBuffers(4, state->ubos);
    for (int i = 0; i < 4; i++) {
        glBindBufferBase(GL_UNIFORM_BUFFER, i, state->ubos[i]);
    }
    // freecam buffer contains a matrix and a bool
    glBindBuffer(GL_UNIFORM_BUFFER, state->freecam_ubo);
    glBufferData(GL_UNIFORM_BUFFER, 17 * 4, nullptr, GL_STATIC_DRAW);
    renderer_gl_update_freecam(state);

    glGenBuffers(12, state->gpu_vbos);
    glGenBuffers(1, &state->gpu_ebo);

    // we have 2 vaos since with geoshaders we need to fallback to sw shaders
    glGenVertexArrays(1, &state->gpu_vao_sw);
    glBindVertexArray(state->gpu_vao_sw);
    glBindBuffer(GL_ARRAY_BUFFER, state->gpu_vbos[0]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state->gpu_ebo);

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

    // for hw vshaders attributes are setup at run time
    glGenVertexArrays(1, &state->gpu_vao_hw);
    glBindVertexArray(state->gpu_vao_hw);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state->gpu_ebo);

    glGenTextures(2, state->screentex);
    glGenFramebuffers(2, state->screenfbo);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, state->screentex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     SCREEN_HEIGHT * ctremu.videoscale,
                     SCREEN_WIDTH(i) * ctremu.videoscale, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, state->screenfbo[i]);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             state->screentex[i], 0);
    }

    glGenTextures(1, &state->swrendertex);
    glGenFramebuffers(1, &state->swrenderfbo);
    glBindTexture(GL_TEXTURE_2D, state->swrendertex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, state->swrenderfbo);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         state->swrendertex, 0);

    // create a blank texture for when games read textures
    // from invalid memory
    // the actual behavior is impossible to emulate in a hw renderer
    // usually its fine to just use a transparent texture instead
    glGenTextures(1, &state->blanktex);
    glBindTexture(GL_TEXTURE_2D, state->blanktex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    // blank texture has no mipmaps
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    // make is always read 0 for all components
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA,
                     (GLint[4]) {GL_ZERO, GL_ZERO, GL_ZERO,
                                 GL_ZERO}); // compound literals are cursed

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

    renderer_gl_async_compiler_init(gpu);
}

void renderer_gl_destroy(GLState* state, GPU* gpu) {
    renderer_gl_async_compiler_destroy(gpu);

    glDeleteProgram(state->main_program);
    glDeleteShader(state->gpu_vs);
    glDeleteShader(state->gpu_uberfs);
    for (int i = 0; i < MAX_PROGRAM; i++) {
        glDeleteProgram(state->progcache.d[i].prog);
    }
    for (int i = 0; i < VSH_MAX; i++) {
        glDeleteShader(gpu->vshaders_hw.d[i].vs);
    }
    for (int i = 0; i < FSH_MAX; i++) {
        glDeleteShader(gpu->fshaders.d[i].fs);
    }
    glDeleteVertexArrays(1, &state->main_vao);
    glDeleteVertexArrays(1, &state->gpu_vao_sw);
    glDeleteVertexArrays(1, &state->gpu_vao_hw);
    glDeleteBuffers(1, &state->main_vbo);
    glDeleteBuffers(12, state->gpu_vbos);
    glDeleteBuffers(4, state->ubos);
    glDeleteBuffers(1, &state->gpu_ebo);
    glDeleteTextures(2, state->screentex);
    glDeleteFramebuffers(2, state->screenfbo);
    glDeleteTextures(1, &state->swrendertex);
    glDeleteFramebuffers(1, &state->swrenderfbo);
    for (int i = 0; i < FB_MAX; i++) {
        glDeleteFramebuffers(1, &gpu->fbs.d[i].fbo);
        glDeleteTextures(1, &gpu->fbs.d[i].color_tex);
        glDeleteTextures(1, &gpu->fbs.d[i].depth_tex);
    }
    for (int i = 0; i < TEX_MAX; i++) {
        glDeleteTextures(1, &gpu->textures.d[i].tex);
    }
}

// call before emulating gpu drawing
void gpu_gl_start_frame(GPU* gpu) {
    glUseProgram(LRU_mru(gpu->gl.progcache)->prog);
    glBindFramebuffer(GL_FRAMEBUFFER, gpu->curfb->fbo);
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

void renderer_gl_update_freecam(GLState* state) {
    glBindBuffer(GL_UNIFORM_BUFFER, state->freecam_ubo);
    if (ctremu.freecam_enable) {
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof ctremu.freecam_mtx,
                        ctremu.freecam_mtx);
        glBufferSubData(GL_UNIFORM_BUFFER, 16 * 4, 4, &(int) {1});
    } else {
        glBufferSubData(GL_UNIFORM_BUFFER, 16 * 4, 4, &(int) {0});
    }
}

static GLuint compile_shader(GLuint type, char* source) {
    auto sh = glCreateShader(type);
    glShaderSource(sh, 1, &(const char*) {source}, nullptr);
    glCompileShader(sh);
    int res;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &res);
    if (!res) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof log, nullptr, log);
        lerror("failed to compile shader: %s", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint link_program(GLState* state, GLuint vs, GLuint fs) {
    auto prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    int res;
    glGetProgramiv(prog, GL_LINK_STATUS, &res);
    if (!res) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof log, nullptr, log);
        lerror("failed to link program: %s", log);
        glDeleteProgram(prog);
        return 0;
    }
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "tex0"), 0);
    glUniform1i(glGetUniformLocation(prog, "tex1"), 1);
    glUniform1i(glGetUniformLocation(prog, "tex2"), 2);
    if (vs != state->gpu_vs) {
        glUniformBlockBinding(prog,
                              glGetUniformBlockIndex(prog, "VertUniforms"), 0);
        glUniformBlockBinding(
            prog, glGetUniformBlockIndex(prog, "FreecamUniforms"), 3);
    }
    if (fs == state->gpu_uberfs)
        glUniformBlockBinding(prog,
                              glGetUniformBlockIndex(prog, "UberUniforms"), 1);
    glUniformBlockBinding(prog, glGetUniformBlockIndex(prog, "FragUniforms"),
                          2);
    return prog;
}

void shader_compilation_thread_func(GPU* gpu) {
    ctremu.thread_gl_setup();

    while (true) {
        pthread_mutex_lock(&gpu->gl.asyncCompiler.mtx);
        gpu->gl.asyncCompiler.busy = false;
        if (gpu->gl.asyncCompiler.die) return;

        while (!gpu->gl.asyncCompiler.busy) {
            pthread_cond_wait(&gpu->gl.asyncCompiler.cv,
                              &gpu->gl.asyncCompiler.mtx);
        }
        pthread_mutex_unlock(&gpu->gl.asyncCompiler.mtx);

        if (gpu->gl.asyncCompiler.die) return;

        auto sh = compile_shader(gpu->gl.asyncCompiler.shaderType,
                                 gpu->gl.asyncCompiler.source);
        free(gpu->gl.asyncCompiler.source);

        if (gpu->gl.asyncCompiler.shaderType == GL_VERTEX_SHADER) {
            auto key = gpu->gl.asyncCompiler.key;
            while (atomic_flag_test_and_set(&gpu->gl.asyncCompiler.lock));
            auto ent = LRU_load(gpu->vshaders_hw, key);
            GLuint old = ent->vs;
            ent->vs = 0;
            atomic_flag_clear(&gpu->gl.asyncCompiler.lock);
            glDeleteShader(old);
            ent->vs = sh;
            ent->hash = key;
        } else if (gpu->gl.asyncCompiler.shaderType == GL_FRAGMENT_SHADER) {
            auto key = gpu->gl.asyncCompiler.key;
            while (atomic_flag_test_and_set(&gpu->gl.asyncCompiler.lock));
            auto ent = LRU_load(gpu->fshaders, key);
            GLuint old = ent->fs;
            ent->fs = 0;
            atomic_flag_clear(&gpu->gl.asyncCompiler.lock);
            glDeleteShader(old);
            ent->fs = sh;
            ent->hash = key;
        }
    }
}

void renderer_gl_async_compiler_init(GPU* gpu) {
    pthread_mutex_init(&gpu->gl.asyncCompiler.mtx, nullptr);
    pthread_cond_init(&gpu->gl.asyncCompiler.cv, nullptr);
    pthread_create(&gpu->gl.asyncCompiler.thd, nullptr,
                   (void*) shader_compilation_thread_func, gpu);
    atomic_flag_clear(&gpu->gl.asyncCompiler.lock);
}

void renderer_gl_async_compiler_destroy(GPU* gpu) {
    pthread_mutex_lock(&gpu->gl.asyncCompiler.mtx);
    gpu->gl.asyncCompiler.die = true;
    gpu->gl.asyncCompiler.busy = true;
    pthread_cond_signal(&gpu->gl.asyncCompiler.cv);
    pthread_mutex_unlock(&gpu->gl.asyncCompiler.mtx);
    pthread_join(gpu->gl.asyncCompiler.thd, nullptr);
    pthread_mutex_destroy(&gpu->gl.asyncCompiler.mtx);
    pthread_cond_destroy(&gpu->gl.asyncCompiler.cv);
}

static void update_cur_fb(GPU* gpu) {
    u32 w = gpu->regs.fb.dim.width;
    u32 h = gpu->regs.fb.dim.height + 1;
    // using the same fb
    if (gpu->curfb->color_paddr == (gpu->regs.fb.colorbuf_loc << 3) &&
        gpu->curfb->width == w && gpu->curfb->height == h)
        return;

    if (gpu->regs.fb.colorbuf_loc == 0) {
        // uhh (if we ignored this it would corrupt the cache)
        lwarn("null framebuffer");
        return;
    }

    // little hack to make arisoturas sm64 port work
    // it clears the depthbuffer by binding it as the colorbuffer
    // and drawing on it
    for (int i = 0; i < FB_MAX; i++) {
        if (gpu->fbs.d[i].depth_paddr == gpu->regs.fb.colorbuf_loc << 3) {
            LRU_use(gpu->fbs, &gpu->fbs.d[i]);
            glBindFramebuffer(GL_FRAMEBUFFER, gpu->fbs.d[i].fbo);
            glClearDepth(0);
            glDepthMask(true);
            glClear(GL_DEPTH_BUFFER_BIT);
            linfo("lmao");
        }
    }

    auto curfb = LRU_load(gpu->fbs, gpu->regs.fb.colorbuf_loc << 3);

    curfb->color_paddr = gpu->regs.fb.colorbuf_loc << 3;
    curfb->depth_paddr = gpu->regs.fb.depthbuf_loc << 3;
    curfb->color_fmt = gpu->regs.fb.colorbuf_fmt.fmt;
    curfb->color_Bpp = gpu->regs.fb.colorbuf_fmt.size + 2;

    linfo("drawing on fb %d at %x with depth buffer at %x", curfb - gpu->fbs.d,
          curfb->color_paddr, curfb->depth_paddr);

    glBindFramebuffer(GL_FRAMEBUFFER, curfb->fbo);

    if (w != curfb->width || h != curfb->height) {
        curfb->width = w;
        curfb->height = h;

        linfo("creating new fb at %08x", curfb->color_paddr);

        glBindTexture(GL_TEXTURE_2D, curfb->color_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     curfb->width * ctremu.videoscale,
                     curfb->height * ctremu.videoscale, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        // framebuffers have no mipmaps
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

        glBindTexture(GL_TEXTURE_2D, curfb->depth_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8,
                     curfb->width * ctremu.videoscale,
                     curfb->height * ctremu.videoscale, 0, GL_DEPTH_STENCIL,
                     GL_UNSIGNED_INT_24_8, nullptr);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, curfb->color_tex, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                               GL_TEXTURE_2D, curfb->depth_tex, 0);
    }

    gpu->curfb = curfb;
}

#define COPYRGBA(dst, src)                                                     \
    ({                                                                         \
        dst[0] = (float) src.r / 255;                                          \
        dst[1] = (float) src.g / 255;                                          \
        dst[2] = (float) src.b / 255;                                          \
        dst[3] = (float) src.a / 255;                                          \
    })

#define COPYRGB(dst, src)                                                      \
    ({                                                                         \
        dst[0] = (float) (src.r & 0xff) / 255;                                 \
        dst[1] = (float) (src.g & 0xff) / 255;                                 \
        dst[2] = (float) (src.b & 0xff) / 255;                                 \
    })

void gpu_gl_display_transfer(GPU* gpu, u32 paddr, int yoff, bool scalex,
                             bool scaley, bool vflip, int screenid) {

    // the source can be offset into or before an existing framebuffer, so we
    // need to account for this
    FBInfo* fb = nullptr;
    int yoffsrc;
    for (int i = 0; i < FB_MAX; i++) {
        if (gpu->fbs.d[i].width == 0) continue;
        yoffsrc = gpu->fbs.d[i].color_paddr - paddr;
        yoffsrc /= (int) (gpu->fbs.d[i].color_Bpp * gpu->fbs.d[i].width);
        if (abs(yoffsrc) < gpu->fbs.d[i].height / 2) {
            fb = &gpu->fbs.d[i];
            break;
        }
    }
    if (!fb) return;
    LRU_use(gpu->fbs, fb);

    linfo("display transfer fb at %x to %s", paddr,
          screenid == SCREEN_TOP ? "top" : "bot");

    // scissor test and color mask affects blit framebuffer
    glDisable(GL_SCISSOR_TEST);
    glColorMask(true, true, true, true);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb->fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gpu->gl.screenfbo[screenid]);

    int srcX0 = 0;
    int srcY0 =
        (fb->height - (SCREEN_WIDTH(screenid) << scaley) + yoff + yoffsrc) *
        ctremu.videoscale;
    int srcX1 = (SCREEN_HEIGHT << scalex) * ctremu.videoscale;
    int srcY1 = (fb->height + yoff + yoffsrc) * ctremu.videoscale;
    int dstX0 = 0;
    int dstY0 = vflip ? SCREEN_WIDTH(screenid) * ctremu.videoscale : 0;
    int dstX1 = SCREEN_HEIGHT * ctremu.videoscale;
    int dstY1 = vflip ? 0 : SCREEN_WIDTH(screenid) * ctremu.videoscale;

    glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // make sure this gets rebound before next draw
    glBindFramebuffer(GL_FRAMEBUFFER, gpu->curfb->fbo);
}

void gpu_gl_render_lcd_fb(GPU* gpu, u32 paddr, u32 fmt, int screenid) {
    linfo("directly rendering lcd fb at %08x to screen %d", paddr, screenid);

    // ngl this whole function is extremely hacky and ignores many things

    void* data = PTR(paddr);

    int colorfmt = fmt & 7;
    GLuint glfmt = (GLuint[8]) {
        GL_RGBA, GL_BGR, GL_RGB, GL_RGBA, GL_RGBA, GL_RGBA, GL_RGBA, GL_RGBA,
    }[colorfmt];
    GLuint gltype = (GLuint[8]) {
        GL_UNSIGNED_INT_8_8_8_8,   GL_UNSIGNED_BYTE,
        GL_UNSIGNED_SHORT_5_6_5,   GL_UNSIGNED_SHORT_5_5_5_1,
        GL_UNSIGNED_SHORT_4_4_4_4, GL_UNSIGNED_INT_8_8_8_8,
        GL_UNSIGNED_INT_8_8_8_8,   GL_UNSIGNED_INT_8_8_8_8,
    }[colorfmt];

    glBindTexture(GL_TEXTURE_2D, gpu->gl.swrendertex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCREEN_HEIGHT,
                 SCREEN_WIDTH(screenid), 0, glfmt, gltype, data);

    // scissor test and color mask affects blit framebuffer
    glDisable(GL_SCISSOR_TEST);
    glColorMask(true, true, true, true);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, gpu->gl.swrenderfbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gpu->gl.screenfbo[screenid]);

    glBlitFramebuffer(0, 0, SCREEN_HEIGHT, SCREEN_WIDTH(screenid), 0,
                      SCREEN_WIDTH(screenid) * ctremu.videoscale,
                      SCREEN_HEIGHT * ctremu.videoscale, 0, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, gpu->curfb->fbo);
}

void gpu_gl_texture_copy(GPU* gpu, u32 srcpaddr, u32 dstpaddr, u32 size,
                         u32 srcpitch, u32 srcgap, u32 dstpitch, u32 dstgap) {

    if (!srcpitch || !dstpitch) return; // why

    auto srcfb = gpu_fbcache_find_within(gpu, srcpaddr);
    auto dsttex = gpu_texcache_find_within(gpu, dstpaddr);

    linfo("texture copy from %x to %x size=%d", srcpaddr, dstpaddr, size);

    if (srcfb && dsttex) {
        // do a hardware copy

        linfo("copying from fb at %x to texture at %x", srcfb->color_paddr,
              dsttex->paddr);

        // need to handle more general cases at some point

        if (srcgap == 0 && dstgap == 0) {
            int yoff = srcpaddr - srcfb->color_paddr + dsttex->paddr - dstpaddr;
            yoff /= (int) (srcfb->width * srcfb->color_Bpp);
            // this can be larger than dst tex height so don't use it
            int transferheight = size / srcfb->width;

            linfo("hardware texture copy sh=%d sw=%d yof=%d th=%d dh=%d dw=%d",
                  srcfb->height, srcfb->width, yoff, transferheight,
                  dsttex->height, dsttex->width);

            glBindFramebuffer(GL_READ_FRAMEBUFFER, srcfb->fbo);
            glBindTexture(GL_TEXTURE_2D, dsttex->tex);
            glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0,
                             (srcfb->height - dsttex->height - yoff) *
                                 ctremu.videoscale,
                             dsttex->width * ctremu.videoscale,
                             dsttex->height * ctremu.videoscale, 0);
        } else {
            linfo("unhandled texture copy case");
        }

        return;
    }

    if (srcfb) {
        linfo("reading back fb into memory");
        // this case requires us to read the framebuffer back to ram
        // it is very slow
        return;
    }

    u8* src = PTR(srcpaddr);
    u8* dst = PTR(dstpaddr);

    int cnt = 0;
    int curline = 0;
    if (srcpitch <= dstpitch) {
        while (cnt < size) {
            memcpy(dst, src, srcpitch);
            cnt += srcpitch;
            curline += srcpitch;
            src += srcpitch + srcgap;
            dst += srcpitch;
            if (curline >= dstpitch) {
                dst += dstgap;
                curline = 0;
            }
        }
    } else {
        while (cnt < size) {
            memcpy(dst, src, dstpitch);
            cnt += dstpitch;
            curline += dstpitch;
            dst += dstpitch + dstgap;
            src += dstpitch;
            if (curline >= srcpitch) {
                src += srcgap;
                curline = 0;
            }
        }
    }

    gpu_invalidate_range(gpu, dstpaddr, size);
}

void gpu_gl_clear_fb(GPU* gpu, u32 paddr, u32 endPaddr, u32 value, u32 datasz) {
    // some of the current gl state can affect gl clear
    // so we need to reset it
    glDisable(GL_SCISSOR_TEST);
    glColorMask(true, true, true, true);
    glDepthMask(true);
    glStencilMask(0xff);
    bool foundDb = false;
    for (int i = 0; i < FB_MAX; i++) {
        if (gpu->fbs.d[i].color_paddr == paddr) {
            LRU_use(gpu->fbs, &gpu->fbs.d[i]);
            gpu->curfb = &gpu->fbs.d[i];

            float r = 0, g = 0, b = 0, a = 1;
            switch (gpu->curfb->color_fmt) {
                case 0:
                    a = (value & 0xff) / 255.f;
                    b = (value >> 8 & 0xff) / 255.f;
                    g = (value >> 16 & 0xff) / 255.f;
                    r = (value >> 24 & 0xff) / 255.f;
                    break;
                case 1:
                    b = (value & 0xff) / 255.f;
                    g = (value >> 8 & 0xff) / 255.f;
                    r = (value >> 16 & 0xff) / 255.f;
                    break;
                case 2:
                    r = (value & 0x1f) / 31.f;
                    g = (value >> 5 & 0x3f) / 63.f;
                    b = (value >> 11 & 0x1f) / 31.f;
                    break;
                case 3:
                    r = (value & 0x1f) / 31.f;
                    g = (value >> 5 & 0x1f) / 31.f;
                    b = (value >> 10 & 0x1f) / 31.f;
                    a = (value >> 15 & 1);
                    break;
                case 4:
                    r = (value & 0xf) / 15.f;
                    g = (value >> 4 & 0xf) / 15.f;
                    b = (value >> 8 & 0xf) / 15.f;
                    a = (value >> 12 & 0xf) / 15.f;
                    break;
            }

            glBindFramebuffer(GL_FRAMEBUFFER, gpu->fbs.d[i].fbo);
            glClearColor(r, g, b, a);
            glClear(GL_COLOR_BUFFER_BIT);
            linfo("cleared color buffer at %x of fb %d with value %x", paddr, i,
                  value);
            return;
        }
        if (gpu->fbs.d[i].depth_paddr == paddr) {
            LRU_use(gpu->fbs, &gpu->fbs.d[i]);
            gpu->curfb = &gpu->fbs.d[i];
            glBindFramebuffer(GL_FRAMEBUFFER, gpu->fbs.d[i].fbo);
            glClearDepth((value & MASK(24)) / (float) BIT(24));
            glClearStencil(value >> 24);
            glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
            linfo("cleared depth buffer at %x of fb %d with value %x", paddr, i,
                  value);
            // dont return size multiple fbs can have the same db
            foundDb = true;
        }
    }

    if (foundDb) return;

    // fallback to sw memfill if no fbs were filled

    linfo("sw memfill at %x to %x value %x datasz %d", paddr, endPaddr, value,
          datasz);

    void* cur = PTR(paddr);
    void* end = PTR(paddr) + endPaddr - paddr;
    switch (datasz) {
        case 2:
            while (cur < end) {
                *(u16*) cur = value;
                cur += 2;
            }
            break;
        case 3:
            while (cur < end) {
                *(u16*) cur = value;
                *(u8*) (cur + 2) = value >> 16;
                cur += 3;
            }
            break;
        case 4:
            while (cur < end) {
                *(u32*) cur = value;
                cur += 4;
            }
            break;
    }

    gpu_invalidate_range(gpu, paddr, endPaddr - paddr);
}

#define LOAD_TEX(t, glfmt, gltype)                                             \
    ({                                                                         \
        t* data = rawdata;                                                     \
                                                                               \
        t pixels[w * h];                                                       \
                                                                               \
        for (int x = 0; x < w; x++) {                                          \
            for (int y = 0; y < h; y++) {                                      \
                pixels[(h - 1 - y) * w + x] = data[morton_swizzle(w, x, y)];   \
            }                                                                  \
        }                                                                      \
                                                                               \
        glTexImage2D(GL_TEXTURE_2D, level, glfmt, w, h, 0, glfmt, gltype,      \
                     pixels);                                                  \
    })

static void* expand_nibbles(u8* src, u32 count, u8* dst) {
    for (int i = 0; i < count; i++) {
        u8 b = src[i / 2];
        if (i & 1) b >>= 4;
        else b &= 0xf;
        b *= 0x11;
        dst[i] = b;
    }
    return dst;
}

typedef struct {
    u8 d[3];
} u24;

static const GLint texswizzle_default[4] = {GL_RED, GL_GREEN, GL_BLUE,
                                            GL_ALPHA};
static const GLint texswizzle_bgr[4] = {GL_BLUE, GL_GREEN, GL_RED, GL_ALPHA};
static const GLint texswizzle_lum_alpha[4] = {GL_GREEN, GL_GREEN, GL_GREEN,
                                              GL_RED};
static const GLint texswizzle_luminance[4] = {GL_RED, GL_RED, GL_RED, GL_ONE};
static const GLint texswizzle_alpha[4] = {GL_ZERO, GL_ZERO, GL_ZERO, GL_RED};

[[maybe_unused]] static const GLint texswizzle_dbg_red[4] = {GL_ONE, GL_ZERO,
                                                             GL_ZERO, GL_ALPHA};
[[maybe_unused]] static const GLint texswizzle_dbg_green[4] = {
    GL_ZERO, GL_ONE, GL_ZERO, GL_ALPHA};
[[maybe_unused]] static const GLint texswizzle_dbg_blue[4] = {GL_ZERO, GL_ZERO,
                                                              GL_ONE, GL_ALPHA};
[[maybe_unused]] static const GLint texswizzle_zero[4] = {GL_ZERO, GL_ZERO,
                                                          GL_ZERO, GL_ZERO};

static const int texfmtbpp[16] = {
    32, 24, 16, 16, 16, 16, 16, 8, 8, 8, 4, 4, 4, 8, 0, 0,
};
static const GLint* texfmtswizzle[16] = {
    texswizzle_default,   texswizzle_bgr,       texswizzle_default,
    texswizzle_default,   texswizzle_default,   texswizzle_lum_alpha,
    texswizzle_default,   texswizzle_luminance, texswizzle_alpha,
    texswizzle_lum_alpha, texswizzle_luminance, texswizzle_alpha,
    texswizzle_default,   texswizzle_default,   texswizzle_default,
    texswizzle_default,
};
static const GLenum texminfilter[4] = {
    GL_NEAREST_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_NEAREST,
    GL_NEAREST_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_LINEAR};
static const GLenum texmagfilter[2] = {GL_NEAREST, GL_LINEAR};

static const GLenum texwrap[4] = {
    GL_CLAMP_TO_EDGE,
    GL_CLAMP_TO_BORDER,
    GL_REPEAT,
    GL_MIRRORED_REPEAT,
};

static const GLenum blend_eq[8] = {
    GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT,
    GL_MIN,      GL_MAX,           GL_FUNC_ADD,
    GL_FUNC_ADD, GL_FUNC_ADD,
};
static const GLenum blend_func[16] = {
    GL_ZERO,
    GL_ONE,
    GL_SRC_COLOR,
    GL_ONE_MINUS_SRC_COLOR,
    GL_DST_COLOR,
    GL_ONE_MINUS_DST_COLOR,
    GL_SRC_ALPHA,
    GL_ONE_MINUS_SRC_ALPHA,
    GL_DST_ALPHA,
    GL_ONE_MINUS_DST_ALPHA,
    GL_CONSTANT_COLOR,
    GL_ONE_MINUS_CONSTANT_COLOR,
    GL_CONSTANT_ALPHA,
    GL_ONE_MINUS_CONSTANT_ALPHA,
    GL_SRC_ALPHA_SATURATE,
    GL_ZERO,
};
static const GLenum logic_ops[16] = {
    GL_CLEAR,         GL_AND,  GL_AND_REVERSE, GL_COPY,         GL_SET,
    GL_COPY_INVERTED, GL_NOOP, GL_INVERT,      GL_NAND,         GL_OR,
    GL_NOR,           GL_XOR,  GL_EQUIV,       GL_AND_INVERTED, GL_OR_REVERSE,
    GL_OR_INVERTED,
};
static const GLenum compare_func[8] = {
    GL_NEVER, GL_ALWAYS, GL_EQUAL,   GL_NOTEQUAL,
    GL_LESS,  GL_LEQUAL, GL_GREATER, GL_GEQUAL,
};
static const GLenum stencil_op[8] = {
    GL_KEEP, GL_ZERO,   GL_REPLACE,   GL_INCR,
    GL_DECR, GL_INVERT, GL_INCR_WRAP, GL_DECR_WRAP,
};

#define TEXSIZE(w, h, fmt, level)                                              \
    (((w) >> (level)) * ((h) >> (level)) * texfmtbpp[fmt] / 8)

// including all mip levels
static inline u32 texsize_total(TexUnitRegs* regs, u32 fmt) {
    u32 size = 0;
    for (int i = regs->lod.min; i <= regs->lod.max; i++) {
        size += TEXSIZE(regs->width, regs->height, fmt, i);
    }
    return size;
}

void load_tex_image(void* rawdata, int w, int h, int level, int fmt) {
    w >>= level;
    h >>= level;
    switch (fmt) {
        case 0: // rgba8888
            LOAD_TEX(u32, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8);
            break;
        case 1: // rgb888
            LOAD_TEX(u24, GL_RGB, GL_UNSIGNED_BYTE);
            break;
        case 2: // rgba5551
            LOAD_TEX(u16, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1);
            break;
        case 3: // rgb565
            LOAD_TEX(u16, GL_RGB, GL_UNSIGNED_SHORT_5_6_5);
            break;
        case 4: // rgba4444
            LOAD_TEX(u16, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4);
            break;
        case 5: // ia88
            LOAD_TEX(u16, GL_RG, GL_UNSIGNED_BYTE);
            break;
        case 6: // hilo8 (rg88)
            LOAD_TEX(u16, GL_RG, GL_UNSIGNED_BYTE);
            break;
        case 7: // i8
            LOAD_TEX(u8, GL_RED, GL_UNSIGNED_BYTE);
            break;
        case 8: // a8
            LOAD_TEX(u8, GL_RED, GL_UNSIGNED_BYTE);
            break;
        case 9: { // ia44
            u8 dec[2 * w * h];
            rawdata = expand_nibbles(rawdata, 2 * w * h, dec);
            LOAD_TEX(u16, GL_RG, GL_UNSIGNED_BYTE);
            break;
        }
        case 10: { // i4
            u8 dec[w * h];
            rawdata = expand_nibbles(rawdata, w * h, dec);
            LOAD_TEX(u8, GL_RED, GL_UNSIGNED_BYTE);
            break;
        }
        case 11: { // a4
            u8 dec[w * h];
            rawdata = expand_nibbles(rawdata, w * h, dec);
            LOAD_TEX(u8, GL_RED, GL_UNSIGNED_BYTE);
            break;
        }
        case 12: { // etc1
            u8 dec[h * w * 3];
            etc1_decompress_texture(w, h, rawdata, (void*) dec);
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, w, h, 0, GL_RGB,
                         GL_UNSIGNED_BYTE, dec);
            break;
        }
        case 13: { // etc1a4
            u8 dec[h * w * 4];
            etc1a4_decompress_texture(w, h, rawdata, (void*) dec);
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, w, h, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, dec);
            break;
        }
        default:
            lerror("unknown texture format %d", fmt);
    }
}

static void create_texture(GPU* gpu, TexInfo* tex, TexUnitRegs* regs) {
    linfo("creating texture from %x with dims %dx%d and fmt=%d", tex->paddr,
          tex->width, tex->height, tex->fmt);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, regs->lod.max);

    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA,
                     texfmtswizzle[tex->fmt]);

    // mipmap images are stored adjacent in memory and each image is
    // half the width and height of the previous one
    void* rawdata = PTR(tex->paddr);
    for (int l = regs->lod.min; l <= regs->lod.max; l++) {
        load_tex_image(rawdata, tex->width, tex->height, l, tex->fmt);
        rawdata += TEXSIZE(tex->width, tex->height, tex->fmt, l);
    }
}

static void load_texture(GPU* gpu, int id, TexUnitRegs* regs, u32 fmt) {
    // make sure we are binding to the correct texture
    glActiveTexture(GL_TEXTURE0 + id);

    // since null is empty for the caches we need to handle
    // null address before searching any cache
    if (regs->addr == 0) {
        linfo("null texture");
        glBindTexture(GL_TEXTURE_2D, gpu->gl.blanktex);
        return;
    }

    u32 texsize = texsize_total(regs, fmt);

    auto tex = LRU_load(gpu->textures, regs->addr << 3);
    glBindTexture(GL_TEXTURE_2D, tex->tex);

    // textures that are partially out of bounds can still be used with rtt ..?
    if (is_valid_physmem(regs->addr << 3) &&
        is_valid_physmem((regs->addr << 3) + texsize - 1)) {
        // if the attributes are different we obviously need to recreate the
        // texture
        // if they are the same we check if the hash needs to be updated
        // and if it does we get the hash and check if that is equal and
        // recreate when it is not
        if (tex->paddr != (regs->addr << 3) || tex->width != regs->width ||
            tex->height != regs->height || tex->fmt != fmt ||
            tex->minlod != regs->lod.min || tex->maxlod != regs->lod.max) {
            tex->paddr = regs->addr << 3;
            tex->width = regs->width;
            tex->height = regs->height;
            tex->fmt = fmt;
            tex->minlod = regs->lod.min;
            tex->maxlod = regs->lod.max;
            tex->size = texsize;

            void* data = PTR(tex->paddr);
            tex->hash = gpu_hash_texture(data, tex->size);
            tex->needs_rehash = false;

            create_texture(gpu, tex, regs);
        } else if (tex->needs_rehash) {
            void* data = PTR(tex->paddr);
            u64 hash = gpu_hash_texture(data, tex->size);
            tex->needs_rehash = false;
            if (hash != tex->hash) {
                tex->hash = hash;
                create_texture(gpu, tex, regs);
            }
        }
    } else {
        // just add this to the cache but dont actually send it image data ig
        // we only care because you can still rtt to this .......
        linfo("out of bounds texture");
        tex->paddr = regs->addr << 3;
        tex->width = regs->width;
        tex->height = regs->height;
        tex->fmt = fmt;
        tex->minlod = regs->lod.min;
        tex->maxlod = regs->lod.max;
        tex->size = texsize;
    }

    // handle simple render to texture cases, but better ...?
    FBInfo* fb = LRU_find(gpu->fbs, tex->paddr);
    if (fb) {
        linfo("rtt at %08x", fb->color_paddr);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fb->fbo);
        glBindTexture(GL_TEXTURE_2D, tex->tex);
        glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0,
                         (fb->height - tex->height) * ctremu.videoscale,
                         tex->width * ctremu.videoscale,
                         tex->height * ctremu.videoscale, 0);
        // no swizzling or mipmaps for rtt textures
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA,
                         texswizzle_default);
    }

    glTexParameteri(
        GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
        texminfilter[regs->param.min_filter | regs->param.mipmapfilter << 1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    texmagfilter[regs->param.mag_filter]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                    texwrap[regs->param.wrap_s]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                    texwrap[regs->param.wrap_t]);
    float bordercolor[4];
    COPYRGBA(bordercolor, regs->border);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, bordercolor);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS,
                    (float) ((int) (regs->lod.bias << 19) >> 19) / 256);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, regs->lod.min);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, regs->lod.max);
}

static void load_texenv(UberUniforms* ubuf, FragUniforms* fbuf, int i,
                        TexEnvRegs* regs) {
    ubuf->tev[i].rgb.src0 = regs->source.rgb0;
    ubuf->tev[i].rgb.src1 = regs->source.rgb1;
    ubuf->tev[i].rgb.src2 = regs->source.rgb2;
    ubuf->tev[i].a.src0 = regs->source.a0;
    ubuf->tev[i].a.src1 = regs->source.a1;
    ubuf->tev[i].a.src2 = regs->source.a2;
    ubuf->tev[i].rgb.op0 = regs->operand.rgb0;
    ubuf->tev[i].rgb.op1 = regs->operand.rgb1;
    ubuf->tev[i].rgb.op2 = regs->operand.rgb2;
    ubuf->tev[i].a.op0 = regs->operand.a0;
    ubuf->tev[i].a.op1 = regs->operand.a1;
    ubuf->tev[i].a.op2 = regs->operand.a2;
    ubuf->tev[i].rgb.combiner = regs->combiner.rgb;
    ubuf->tev[i].a.combiner = regs->combiner.a;
    COPYRGBA(fbuf->tev_color[i], regs->color);
    ubuf->tev[i].rgb.scale = 1 << (regs->scale.rgb);
    ubuf->tev[i].a.scale = 1 << (regs->scale.a);
}

static const GLuint attrtypes[] = {
    GL_BYTE,
    GL_UNSIGNED_BYTE,
    GL_SHORT,
    GL_FLOAT,
};

static void setup_fixattrs_hw(GPU* gpu) {
    for (int i = 0; i < 12; i++) {
        if (gpu->regs.geom.fixed_attr_mask & BIT(i)) {
            int attr = (gpu->regs.vsh.permutation >> 4 * i) & 0xf;
            glVertexAttrib4fv(attr, gpu->fixattrs[i]);
            glDisableVertexAttribArray(attr);
        }
    }
}

static void setup_hw_vao(GPU* gpu, int start, int num) {
    setup_fixattrs_hw(gpu);

    for (int vbo = 0; vbo < 12; vbo++) {
        // skip unused vbos
        if (gpu->regs.geom.attrbuf[vbo].count == 0) continue;

        glBindBuffer(GL_ARRAY_BUFFER, gpu->gl.gpu_vbos[vbo]);

        void* data = PTR(gpu->regs.geom.attr_base * 8 +
                         gpu->regs.geom.attrbuf[vbo].offset);
        void* off = nullptr;
        u32 stride = gpu->regs.geom.attrbuf[vbo].size;

        for (int c = 0; c < gpu->regs.geom.attrbuf[vbo].count; c++) {
            int attr = (gpu->regs.geom.attrbuf[vbo].comp >> 4 * c) & 0xf;
            if (attr >= 0xc) {
                off += 4 * (attr - 0xb);
                continue;
            }
            int fmt = (gpu->regs.geom.attr_format >> 4 * attr) & 0xf;

            int size = (fmt >> 2) + 1;
            int type = fmt & 3;

            int permattr = (gpu->regs.vsh.permutation >> 4 * attr) & 0xf;

            glVertexAttribPointer(permattr, size, attrtypes[type], GL_FALSE,
                                  stride, off);
            glEnableVertexAttribArray(permattr);

            static const int typesize[4] = {1, 1, 2, 4};
            off += size * typesize[type];
        }

        glBufferData(GL_ARRAY_BUFFER, num * stride, data + (start * stride),
                     GL_STREAM_DRAW);
    }
}

static void setup_hw_vao_imm(GPU* gpu) {
    setup_fixattrs_hw(gpu);

    int nattrs = gpu->regs.geom.vsh_num_attr + 1;
    // only need to use one vbo
    glBindBuffer(GL_ARRAY_BUFFER, gpu->gl.gpu_vbos[0]);
    for (int i = 0; i < nattrs; i++) {
        int attr = (gpu->regs.vsh.permutation >> 4 * i) & 0xf;
        glVertexAttribPointer(attr, 4, GL_FLOAT, GL_FALSE,
                              nattrs * sizeof(fvec4),
                              (void*) (i * sizeof(fvec4)));
        glEnableVertexAttribArray(attr);
    }
    glBufferData(GL_ARRAY_BUFFER, gpu->immattrs.size * sizeof(fvec4),
                 gpu->immattrs.d, GL_STREAM_DRAW);
}

static const GLenum prim_mode[4] = {
    GL_TRIANGLES,
    GL_TRIANGLE_STRIP,
    GL_TRIANGLE_FAN,
    GL_TRIANGLES,
};

static const GLuint indextypes[2] = {GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT};

void gpu_gl_draw(GPU* gpu, bool elements, bool immediate) {
    int nattrs = gpu->regs.geom.vsh_num_attr + 1;
    int nverts =
        immediate ? gpu->immattrs.size / nattrs : gpu->regs.geom.nverts;
    int primMode = gpu->regs.geom.prim_config.mode;

    linfo("drawing %s %s nverts=%d, prim mode=%d",
          elements ? "elements" : "arrays", immediate ? "immediate mode" : "",
          nverts, primMode);

    update_cur_fb(gpu);

    // ensure unused entries are 0 so the hashing is consistent
    UberUniforms ubuf = {};
    FragUniforms fbuf;

    // cull mode
    switch (gpu->regs.raster.cullmode) {
        case 0:
        case 3:
            glDisable(GL_CULL_FACE);
            break;
        case 1:
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);
            break;
        case 2:
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            break;
    }

    // viewport and scissor
    glViewport(gpu->regs.raster.view_x * ctremu.videoscale,
               gpu->regs.raster.view_y * ctremu.videoscale,
               2 * cvtf24(gpu->regs.raster.view_w) * ctremu.videoscale,
               2 * cvtf24(gpu->regs.raster.view_h) * ctremu.videoscale);
    if (gpu->regs.raster.scisssortest.enable) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(gpu->regs.raster.scisssortest.x1 * ctremu.videoscale,
                  gpu->regs.raster.scisssortest.y1 * ctremu.videoscale,
                  (gpu->regs.raster.scisssortest.x2 + 1 -
                   gpu->regs.raster.scisssortest.x1) *
                      ctremu.videoscale,
                  (gpu->regs.raster.scisssortest.y2 + 1 -
                   gpu->regs.raster.scisssortest.y1) *
                      ctremu.videoscale);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }

    // depth map
    if (gpu->regs.raster.depthmap_enable) {
        float offset = cvtf24(gpu->regs.raster.depthmap_offset);
        float scale = cvtf24(gpu->regs.raster.depthmap_scale);
        // pica near plane is -1 and farplane is 0
        glDepthRange(offset - scale, offset);
    } else {
        // default depth range maps -1 -> 1 and 0 -> 0
        glDepthRange(1, 0);
    }

    // textures
    ubuf.tex2coord = gpu->regs.tex.config.tex2coord;
    if (gpu->regs.tex.config.tex0enable) {
        load_texture(gpu, 0, &gpu->regs.tex.tex0, gpu->regs.tex.tex0_fmt);
    }
    if (gpu->regs.tex.config.tex1enable) {
        load_texture(gpu, 1, &gpu->regs.tex.tex1, gpu->regs.tex.tex1_fmt);
    }
    if (gpu->regs.tex.config.tex2enable) {
        load_texture(gpu, 2, &gpu->regs.tex.tex2, gpu->regs.tex.tex2_fmt);
    }

    // texenvs
    load_texenv(&ubuf, &fbuf, 0, &gpu->regs.tex.tev0);
    load_texenv(&ubuf, &fbuf, 1, &gpu->regs.tex.tev1);
    load_texenv(&ubuf, &fbuf, 2, &gpu->regs.tex.tev2);
    load_texenv(&ubuf, &fbuf, 3, &gpu->regs.tex.tev3);
    load_texenv(&ubuf, &fbuf, 4, &gpu->regs.tex.tev4);
    load_texenv(&ubuf, &fbuf, 5, &gpu->regs.tex.tev5);
    ubuf.tev_update_rgb = gpu->regs.tex.tev_buffer.update_rgb;
    ubuf.tev_update_alpha = gpu->regs.tex.tev_buffer.update_alpha;
    COPYRGBA(fbuf.tev_buffer_color, gpu->regs.tex.tev5.buffer_color);

    // blending/logic ops
    if (gpu->regs.fb.color_op.frag_mode != 0) {
        // gas or shadows, not implemented
        linfo("unknown frag mode %d", gpu->regs.fb.color_op.frag_mode);
        return;
    }
    if (gpu->regs.fb.color_op.blend_mode) {
        glDisable(GL_COLOR_LOGIC_OP);
        glEnable(GL_BLEND);
        glBlendEquationSeparate(blend_eq[gpu->regs.fb.blend_func.rgb_eq],
                                blend_eq[gpu->regs.fb.blend_func.a_eq]);
        glBlendFuncSeparate(blend_func[gpu->regs.fb.blend_func.rgb_src],
                            blend_func[gpu->regs.fb.blend_func.rgb_dst],
                            blend_func[gpu->regs.fb.blend_func.a_src],
                            blend_func[gpu->regs.fb.blend_func.a_dst]);
        glBlendColor(gpu->regs.fb.blend_color.r / 255.f,
                     gpu->regs.fb.blend_color.g / 255.f,
                     gpu->regs.fb.blend_color.b / 255.f,
                     gpu->regs.fb.blend_color.a / 255.f);
    } else {
        glDisable(GL_BLEND);
        glEnable(GL_COLOR_LOGIC_OP);
        glLogicOp(logic_ops[gpu->regs.fb.logic_op]);
    }

    // alpha test
    ubuf.alphatest = gpu->regs.fb.alpha_test.enable;
    ubuf.alphafunc = gpu->regs.fb.alpha_test.func;
    fbuf.alpharef = (float) gpu->regs.fb.alpha_test.ref / 255;

    // stencil test
    if (gpu->regs.fb.stencil_test.enable) {
        glEnable(GL_STENCIL_TEST);
        if (gpu->regs.fb.perms.depthbuf.write) {
            glStencilMask(gpu->regs.fb.stencil_test.bufmask);
        } else {
            glStencilMask(0);
        }
        glStencilFunc(compare_func[gpu->regs.fb.stencil_test.func],
                      gpu->regs.fb.stencil_test.ref,
                      gpu->regs.fb.stencil_test.mask);
        glStencilOp(stencil_op[gpu->regs.fb.stencil_op.fail],
                    stencil_op[gpu->regs.fb.stencil_op.zfail],
                    stencil_op[gpu->regs.fb.stencil_op.zpass]);
    } else {
        glDisable(GL_STENCIL_TEST);
    }

    // color mask and depth mask
    if (gpu->regs.fb.perms.colorbuf.write) {
        glColorMask(gpu->regs.fb.color_mask.red, gpu->regs.fb.color_mask.green,
                    gpu->regs.fb.color_mask.blue,
                    gpu->regs.fb.color_mask.alpha);
    } else {
        glColorMask(false, false, false, false);
    }
    // you can disable writing to the depth buffer with this register
    // instead of using the depth mask
    if (gpu->regs.fb.perms.depthbuf.write) {
        glDepthMask(gpu->regs.fb.color_mask.depth);
    } else {
        glDepthMask(false);
    }

    // depth test
    // we need to always enable the depth test, since the pica can still
    // write the depth buffer even if depth testing is disabled
    glEnable(GL_DEPTH_TEST);
    if (gpu->regs.fb.color_mask.depthtest) {
        glDepthFunc(compare_func[gpu->regs.fb.color_mask.depthfunc]);
    } else {
        glDepthFunc(GL_ALWAYS);
    }

    // lighting params
    ubuf.numlights = gpu->regs.lighting.numlights + 1;
    for (int i = 0; i < ubuf.numlights; i++) {
        // TODO: handle light permutation
        COPYRGB(fbuf.light[i].specular0, gpu->regs.lighting.light[i].specular0);
        COPYRGB(fbuf.light[i].specular1, gpu->regs.lighting.light[i].specular1);
        COPYRGB(fbuf.light[i].diffuse, gpu->regs.lighting.light[i].diffuse);
        COPYRGB(fbuf.light[i].ambient, gpu->regs.lighting.light[i].ambient);
        fbuf.light[i].vec[0] = cvtf16(gpu->regs.lighting.light[i].vec.x);
        fbuf.light[i].vec[1] = cvtf16(gpu->regs.lighting.light[i].vec.y);
        fbuf.light[i].vec[2] = cvtf16(gpu->regs.lighting.light[i].vec.z);
        ubuf.light[i].config = gpu->regs.lighting.light[i].config;
    }
    COPYRGB(fbuf.ambient_color, gpu->regs.lighting.ambient);

    // vertex shaders
    bool swshaders = !ctremu.hwvshaders || gpu->regs.geom.config.use_gsh;
    GLuint vs = gpu->gl.gpu_vs;
    if (!swshaders) {
        if (gpu->vsh.code_dirty) {
            u64 hash = gpu_hash_hw_shader(gpu);
            if (ctremu.asyncshadercompilation) {
                while (atomic_flag_test_and_set(&gpu->gl.asyncCompiler.lock));
                auto ent = LRU_find(gpu->vshaders_hw, hash);
                atomic_flag_clear(&gpu->gl.asyncCompiler.lock);
                if (!ent) {
                    pthread_mutex_lock(&gpu->gl.asyncCompiler.mtx);
                    if (!gpu->gl.asyncCompiler.busy) {
                        char* source = shader_dec_vs(gpu);

                        gpu->gl.asyncCompiler.shaderType = GL_VERTEX_SHADER;
                        gpu->gl.asyncCompiler.key = hash;
                        gpu->gl.asyncCompiler.source = source;
                        gpu->gl.asyncCompiler.busy = true;
                        pthread_cond_signal(&gpu->gl.asyncCompiler.cv);
                        linfo(
                            "async compiling new vertex shader with hash %llx",
                            hash);
                    }
                    pthread_mutex_unlock(&gpu->gl.asyncCompiler.mtx);

                    swshaders = true;
                } else {
                    vs = ent->vs;
                }
            } else {
                auto ent = LRU_load(gpu->vshaders_hw, hash);
                if (ent->hash != hash) {
                    char* source = shader_dec_vs(gpu);

                    ent->hash = hash;
                    glDeleteShader(ent->vs);
                    ent->vs = compile_shader(GL_VERTEX_SHADER, source);
                    free(source);
                    linfo("compiled new vertex shader with hash %llx", hash);
                }
                vs = ent->vs;
            }
        } else {
            vs = LRU_mru(gpu->vshaders_hw)->vs;
        }
    }

    if (swshaders) {
        glBindVertexArray(gpu->gl.gpu_vao_sw);
        glBindBuffer(GL_ARRAY_BUFFER, gpu->gl.gpu_vbos[0]);
    } else {
        if (gpu->vsh_uniform_dirty) {
            gpu->vsh_uniform_dirty = false;
            VertUniforms vubuf;
            memcpy(vubuf.c, gpu->vsh.floatuniform, sizeof vubuf.c);
            // expand intuniform from bytes to ints
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    vubuf.i[i][j] = gpu->regs.vsh.intuniform[i][j];
                }
            }
            vubuf.b_raw = gpu->regs.vsh.booluniform;
            glBindBuffer(GL_UNIFORM_BUFFER, gpu->gl.vert_ubo);
            glBufferData(GL_UNIFORM_BUFFER, sizeof vubuf, &vubuf,
                         GL_DYNAMIC_DRAW);
        }
        glBindVertexArray(gpu->gl.gpu_vao_hw);
    }

    // fragment shaders
    // todo: do similar dirty checking for the fs
    glBindBuffer(GL_UNIFORM_BUFFER, gpu->gl.frag_ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof fbuf, &fbuf, GL_STREAM_DRAW);
    bool ubershader = ctremu.ubershader;
    GLuint fs = gpu->gl.gpu_uberfs;
    if (!ubershader) {
        u64 hash = gpu_hash_fs(&ubuf);
        if (ctremu.asyncshadercompilation) {
            while (atomic_flag_test_and_set(&gpu->gl.asyncCompiler.lock));
            auto ent = LRU_find(gpu->fshaders, hash);
            atomic_flag_clear(&gpu->gl.asyncCompiler.lock);
            if (!ent) {
                pthread_mutex_lock(&gpu->gl.asyncCompiler.mtx);
                if (!gpu->gl.asyncCompiler.busy) {
                    char* source = shader_gen_fs(&ubuf);

                    gpu->gl.asyncCompiler.shaderType = GL_FRAGMENT_SHADER;
                    gpu->gl.asyncCompiler.key = hash;
                    gpu->gl.asyncCompiler.source = source;
                    gpu->gl.asyncCompiler.busy = true;
                    pthread_cond_signal(&gpu->gl.asyncCompiler.cv);
                    linfo("async compiling new fragment shader with hash %llx",
                          hash);
                }
                pthread_mutex_unlock(&gpu->gl.asyncCompiler.mtx);

                ubershader = true;
            } else {
                fs = ent->fs;
            }
        } else {
            auto ent = LRU_load(gpu->fshaders, hash);
            if (ent->hash != hash) {
                ent->hash = hash;
                char* source = shader_gen_fs(&ubuf);

                glDeleteShader(ent->fs);
                ent->fs = compile_shader(GL_FRAGMENT_SHADER, source);
                free(source);
                linfo("compiled new fragment shader with hash %llx", hash);
            }
            fs = ent->fs;
        }
    }
    if (ubershader) {
        glBindBuffer(GL_UNIFORM_BUFFER, gpu->gl.uber_ubo);
        glBufferData(GL_UNIFORM_BUFFER, sizeof ubuf, &ubuf, GL_STREAM_DRAW);
    }

    // finally get the program
    if (LRU_mru(gpu->gl.progcache)->vs != vs ||
        LRU_mru(gpu->gl.progcache)->fs != fs) {
        auto ent = LRU_load(gpu->gl.progcache, vs | ((u64) fs << 32));
        if (ent->vs != vs || ent->fs != fs) {
            glDeleteProgram(ent->prog);
            ent->vs = vs;
            ent->fs = fs;
            ent->prog = link_program(&gpu->gl, vs, fs);
            linfo("linked new program from vs %d and fs %d", ent->vs, ent->fs);
        } else {
            glUseProgram(ent->prog);
        }
    }

    // starting  index
    int basevert = immediate ? 0 : gpu->regs.geom.vtx_off;
    // how many verts are sent to the VS (can be more than nverts for
    // drawelements)
    int nbufverts = nverts;

    // setup index buffer and find min/max index
    void* indexbuf = nullptr;
    bool indexsize = gpu->regs.geom.indexfmt;
    if (elements) {
        u32 minind = 0xffff, maxind = 0;
        indexbuf =
            PTR(gpu->regs.geom.attr_base * 8 + gpu->regs.geom.indexbufoff);
        for (int i = 0; i < gpu->regs.geom.nverts; i++) {
            int idx;
            if (indexsize) {
                idx = ((u16*) indexbuf)[i];
            } else {
                idx = ((u8*) indexbuf)[i];
            }
            if (idx < minind) minind = idx;
            if (idx > maxind) maxind = idx;
        }
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, nverts * BIT(indexsize), indexbuf,
                     GL_STREAM_DRAW);
        // update these since we are drawing elements
        basevert = minind;
        nbufverts = maxind + 1 - minind;
    }

    if (swshaders) {
        fvec4 vshout[nbufverts][16];
        // run the vertex shader
        gpu_run_vsh(gpu, immediate, basevert, nbufverts, vshout);

        // fshin is either vshout or gshout
        fvec4(*fshin)[16] = vshout;

        // run the geometry shader if enabled
        ShaderUnit gsh;
        gpu_init_gsh(gpu, &gsh);
        if (gpu->regs.geom.config.use_gsh) {
            gpu_run_gsh(gpu, &gsh, elements, basevert, nverts, vshout,
                        indexsize, indexbuf);

            // since we are drawing geometry shader output, these are changed
            // appriopriately
            fshin = gsh.gsh.outvtx.d;
            basevert = 0;
            nverts = gsh.gsh.outvtx.size;
            nbufverts = nverts;
            elements = false;
        }

        // use the outmap config to setup the final vertex buffer sent to gpu
        // for the fragment shader
        Vertex vbuf[nbufverts];
        for (int i = 0; i < nbufverts; i++) {
            gpu_write_outmap_vtx(gpu, &vbuf[i], fshin[i]);
        }
        Vec_free(gsh.gsh.outvtx);

        glBufferData(GL_ARRAY_BUFFER, sizeof vbuf, vbuf, GL_STREAM_DRAW);
    } else {
        if (immediate) {
            setup_hw_vao_imm(gpu);
        } else {
            setup_hw_vao(gpu, basevert, nbufverts);
        }
    }
    Vec_free(gpu->immattrs);

    // finally do the draw call
    if (elements) {
        glDrawElementsBaseVertex(prim_mode[primMode], nverts,
                                 indextypes[indexsize], 0, -basevert);
    } else {
        glDrawArrays(prim_mode[primMode], 0, nverts);
    }
}