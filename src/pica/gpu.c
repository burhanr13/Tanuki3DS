#include "gpu.h"

#include "../3ds.h"
#include "etc1.h"
#include "renderer_gl.h"
#include "shader.h"

#define SHADERJIT

#undef PTR
#define PTR(addr) ((void*) &gpu->mem[addr])

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
static const GLenum prim_mode[4] = {
    GL_TRIANGLES,
    GL_TRIANGLE_STRIP,
    GL_TRIANGLE_FAN,
    GL_TRIANGLES,
};

#define CONVERTFLOAT(e, m, i)                                                  \
    ({                                                                         \
        u32 sgn = (i >> (e + m)) & 1;                                          \
        u32 exp = (i >> m) & MASK(e);                                          \
        u32 mantissa = i & MASK(m);                                            \
        if (exp == 0 && mantissa == 0) {                                       \
            exp = 0;                                                           \
        } else if (exp == MASK(e)) {                                           \
            exp = 0xff;                                                        \
        } else {                                                               \
            exp += BIT(7) - BIT(e - 1);                                        \
        }                                                                      \
        mantissa <<= 23 - m;                                                   \
        I2F(sgn << 31 | exp << 23 | mantissa);                                 \
    })

float cvtf24(u32 i) {
    return CONVERTFLOAT(7, 16, i);
}

float cvtf16(u32 i) {
    return CONVERTFLOAT(5, 10, i);
}

bool is_valid_physmem(u32 addr) {
    return (VRAM_PBASE <= addr && addr < VRAM_PBASE + VRAMSIZE) ||
           (FCRAM_PBASE <= addr && addr < FCRAM_PBASE + FCRAMSIZE);
}

void gpu_write_internalreg(GPU* gpu, u16 id, u32 param, u32 mask) {
    if (id >= GPUREG_MAX) {
        lerror("out of bounds gpu reg");
        return;
    }
    linfo("command %03x (0x%08x) & %08x (%f)", id, param, mask, I2F(param));
    gpu->io.w[id] &= ~mask;
    gpu->io.w[id] |= param & mask;
    switch (id) {
        // this is a slow way to maybe ensure texture cache coherency
        // case GPUREG(tex.config):
        //     if (gpu->io.tex.config.clearcache) {
        //         while (gpu->textures.size) {
        //             TexInfo* t = LRU_eject(gpu->textures);
        //             t->paddr = 0;
        //             t->width = 0;
        //             t->height = 0;
        //         }
        //     }
        //     break;
        case GPUREG(geom.drawarrays):
            gpu_update_gl_state(gpu);
            gpu_drawarrays(gpu);
            break;
        case GPUREG(geom.drawelements):
            gpu_update_gl_state(gpu);
            gpu_drawelements(gpu);
            break;
        case GPUREG(geom.fixattr_data[0])... GPUREG(geom.fixattr_data[2]): {
            fvec* fattr;
            bool immediatemode = false;
            if (gpu->io.geom.fixattr_idx == 0xf) {
                if (gpu->immattrs.size == gpu->immattrs.cap) {
                    gpu->immattrs.cap =
                        gpu->immattrs.cap ? 2 * gpu->immattrs.cap : 8;
                    gpu->immattrs.d = realloc(gpu->immattrs.d,
                                              gpu->immattrs.cap * sizeof(fvec));
                }
                fattr = &gpu->immattrs.d[gpu->immattrs.size];
                immediatemode = true;
            } else {
                fattr = &gpu->fixattrs[gpu->io.geom.fixattr_idx];
            }
            switch (gpu->curfixi) {
                case 0: {
                    (*fattr)[3] = cvtf24(param >> 8);
                    gpu->curfixattr = (param & 0xff) << 16;
                    gpu->curfixi = 1;
                    break;
                }
                case 1: {
                    (*fattr)[2] = cvtf24(param >> 16 | gpu->curfixattr);
                    gpu->curfixattr = (param & MASK(16)) << 8;
                    gpu->curfixi = 2;
                    break;
                }
                case 2: {
                    (*fattr)[1] = cvtf24(param >> 24 | gpu->curfixattr);
                    (*fattr)[0] = cvtf24(param & MASK(24));
                    gpu->curfixi = 0;
                    if (immediatemode) gpu->immattrs.size++;
                    break;
                }
            }
            break;
        }
        case GPUREG(geom.start_draw_func0):
            // this register must be written to after any draw call so we can
            // use it to end an immediate draw call, since there is no explicit
            // way to end an immediate mode draw call like glEnd
            if (gpu->immattrs.size) {
                gpu_update_gl_state(gpu);
                gpu_drawimmediate(gpu);
            }
            break;
        case GPUREG(vsh.floatuniform_data[0])... GPUREG(
            vsh.floatuniform_data[7]): {
            u32 idx = gpu->io.vsh.floatuniform_idx & 7;
            if (idx >= 96) {
                lwarn("writing to out of bound uniform");
                break;
            }
            fvec* uniform = &gpu->floatuniform[gpu->io.vsh.floatuniform_idx];
            if (gpu->io.vsh.floatuniform_mode) {
                (*uniform)[3 - gpu->curunifi] = I2F(param);
                if (++gpu->curunifi == 4) {
                    gpu->curunifi = 0;
                    gpu->io.vsh.floatuniform_idx++;
                }
            } else {
                switch (gpu->curunifi) {
                    case 0: {
                        (*uniform)[3] = cvtf24(param >> 8);
                        gpu->curuniform = (param & 0xff) << 16;
                        gpu->curunifi = 1;
                        break;
                    }
                    case 1: {
                        (*uniform)[2] = cvtf24(param >> 16 | gpu->curuniform);
                        gpu->curuniform = (param & MASK(16)) << 8;
                        gpu->curunifi = 2;
                        break;
                    }
                    case 2: {
                        (*uniform)[1] = cvtf24(param >> 24 | gpu->curuniform);
                        (*uniform)[0] = cvtf24(param & MASK(24));
                        gpu->curunifi = 0;
                        gpu->io.vsh.floatuniform_idx++;
                        break;
                    }
                }
            }
            break;
        }
        case GPUREG(vsh.entrypoint):
            gpu->sh_dirty = true;
            break;
        case GPUREG(vsh.codetrans_data[0])... GPUREG(vsh.codetrans_data[8]):
            gpu->sh_dirty = true;
            gpu->progdata[gpu->io.vsh.codetrans_idx++ % SHADER_CODE_SIZE] =
                param;
            break;
        case GPUREG(vsh.opdescs_data[0])... GPUREG(vsh.opdescs_data[8]):
            gpu->sh_dirty = true;
            gpu->opdescs[gpu->io.vsh.opdescs_idx++ % SHADER_OPDESC_SIZE] =
                param;
            break;
        case GPUREG(geom.restart_primitive):
            Vec_free(gpu->immattrs);
            break;
    }
}

#define NESTED_CMDLIST()                                                       \
    ({                                                                         \
        switch (c.id) {                                                        \
            case GPUREG(geom.cmdbuf.jmp[0]):                                   \
                gpu_run_command_list(gpu, gpu->io.geom.cmdbuf.addr[0] << 3,    \
                                     gpu->io.geom.cmdbuf.size[0] << 3);        \
                return;                                                        \
            case GPUREG(geom.cmdbuf.jmp[1]):                                   \
                gpu_run_command_list(gpu, gpu->io.geom.cmdbuf.addr[1] << 3,    \
                                     gpu->io.geom.cmdbuf.size[1] << 3);        \
                return;                                                        \
        }                                                                      \
    })

void gpu_run_command_list(GPU* gpu, u32 paddr, u32 size) {
    gpu->cur_fb = NULL;

    paddr &= ~15;
    size &= ~15;

    u32* cmds = PTR(paddr);

    u32* cur = cmds;
    u32* end = cmds + (size / 4);
    while (cur < end) {
        GPUCommand c = {cur[1]};
        u32 mask = 0;
        if (c.mask & BIT(0)) mask |= 0xff << 0;
        if (c.mask & BIT(1)) mask |= 0xff << 8;
        if (c.mask & BIT(2)) mask |= 0xff << 16;
        if (c.mask & BIT(3)) mask |= 0xff << 24;

        // nested command lists are jumps, so we need to handle them over here
        // to avoid possible stack overflow
        NESTED_CMDLIST();
        gpu_write_internalreg(gpu, c.id, cur[0], mask);
        cur += 2;
        if (c.incmode) c.id++;
        for (int i = 0; i < c.nparams; i++) {
            NESTED_CMDLIST();
            gpu_write_internalreg(gpu, c.id, *cur++, mask);
            if (c.incmode) c.id++;
        }
        // each command must be 8 byte aligned
        if (c.nparams & 1) cur++;
    }
}

// searches the framebuffer cache and return NULL if not found
FBInfo* fbcache_find(GPU* gpu, u32 color_paddr) {
    FBInfo* newfb = NULL;
    for (int i = 0; i < FB_MAX; i++) {
        if (gpu->fbs.d[i].color_paddr == color_paddr) {
            newfb = &gpu->fbs.d[i];
            break;
        }
    }
    if (newfb) LRU_use(gpu->fbs, newfb);
    return newfb;
}

// searches the framebuffer cache and creates a new framebuffer if not found
FBInfo* fbcache_load(GPU* gpu, u32 color_paddr) {
    FBInfo* newfb = NULL;
    for (int i = 0; i < FB_MAX; i++) {
        if (gpu->fbs.d[i].color_paddr == color_paddr ||
            gpu->fbs.d[i].color_paddr == 0) {
            newfb = &gpu->fbs.d[i];
            break;
        }
    }
    if (!newfb) {
        newfb = LRU_eject(gpu->fbs);
        newfb->depth_paddr = 0;
        newfb->width = 0;
        newfb->height = 0;
    }
    newfb->color_paddr = color_paddr;
    LRU_use(gpu->fbs, newfb);
    return newfb;
}

void gpu_update_cur_fb(GPU* gpu) {
    if (gpu->cur_fb &&
        gpu->cur_fb->color_paddr == (gpu->io.fb.colorbuf_loc << 3))
        return;

    gpu->cur_fb = fbcache_load(gpu, gpu->io.fb.colorbuf_loc << 3);
    gpu->cur_fb->depth_paddr = gpu->io.fb.depthbuf_loc << 3;
    gpu->cur_fb->color_fmt = gpu->io.fb.colorbuf_fmt.fmt & 7;
    gpu->cur_fb->color_Bpp = gpu->io.fb.colorbuf_fmt.size + 2;

    linfo("drawing on fb %d at %x", gpu->cur_fb - gpu->fbs.d,
          gpu->cur_fb->color_paddr);

    glBindFramebuffer(GL_FRAMEBUFFER, gpu->cur_fb->fbo);

    u32 w = gpu->io.fb.dim.width;
    u32 h = gpu->io.fb.dim.height + 1;

    if (w != gpu->cur_fb->width || h != gpu->cur_fb->height) {
        gpu->cur_fb->width = w;
        gpu->cur_fb->height = h;

        glBindTexture(GL_TEXTURE_2D, gpu->cur_fb->color_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gpu->cur_fb->width * g_upscale,
                     gpu->cur_fb->height * g_upscale, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, NULL);
        glBindTexture(GL_TEXTURE_2D, gpu->cur_fb->depth_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8,
                     gpu->cur_fb->width * g_upscale,
                     gpu->cur_fb->height * g_upscale, 0, GL_DEPTH_STENCIL,
                     GL_UNSIGNED_INT_24_8, NULL);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, gpu->cur_fb->color_tex, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                               GL_TEXTURE_2D, gpu->cur_fb->depth_tex, 0);

        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
}

void gpu_display_transfer(GPU* gpu, u32 paddr, int yoff, bool scalex,
                          bool scaley, bool top) {
    FBInfo* fb = NULL;
    int yoffsrc;
    // the source is often offset into an existing framebuffer, so we need to
    // account for this
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

    linfo("display transfer fb at %x to %s", paddr, top ? "top" : "bot");

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                      top ? gpu->gl.fbotop : gpu->gl.fbobot);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb->fbo);
    u32 scwidth = top ? SCREEN_WIDTH : SCREEN_WIDTH_BOT;
    glBlitFramebuffer(
        0, (fb->height - scwidth + yoff + yoffsrc) * g_upscale,
        (SCREEN_HEIGHT << scalex) * g_upscale,
        (fb->height - scwidth + yoff + yoffsrc + (scwidth << scaley)) *
            g_upscale,
        0, 0, SCREEN_HEIGHT * g_upscale, scwidth * g_upscale,
        GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

void gpu_clear_fb(GPU* gpu, u32 paddr, u32 color) {
    for (int i = 0; i < FB_MAX; i++) {
        if (gpu->fbs.d[i].color_paddr == paddr) {
            LRU_use(gpu->fbs, &gpu->fbs.d[i]);
            glBindFramebuffer(GL_FRAMEBUFFER, gpu->fbs.d[i].fbo);
            glClearColor((color >> 24) / 256.f, ((color >> 16) & 0xff) / 256.f,
                         ((color >> 8) & 0xff) / 256.f, (color & 0xff) / 256.f);
            glClear(GL_COLOR_BUFFER_BIT);
            linfo("cleared color buffer at %x of fb %d with value %x", paddr, i,
                  color);
            return;
        }
        if (gpu->fbs.d[i].depth_paddr == paddr) {
            LRU_use(gpu->fbs, &gpu->fbs.d[i]);
            glBindFramebuffer(GL_FRAMEBUFFER, gpu->fbs.d[i].fbo);
            glClearDepthf((color & MASK(24)) / (float) (1 << 24));
            glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
            linfo("cleared depth buffer at %x of fb %d with value %x", paddr, i,
                  color);
            return;
        }
    }
}

// ensures there is a texture with the given paddr in the cache
TexInfo* texcache_load(GPU* gpu, u32 paddr) {
    TexInfo* tex = NULL;
    for (int i = 0; i < TEX_MAX; i++) {
        if (gpu->textures.d[i].paddr == paddr ||
            gpu->textures.d[i].paddr == 0) {
            tex = &gpu->textures.d[i];
            break;
        }
    }
    if (!tex) {
        tex = LRU_eject(gpu->textures);
        tex->width = 0;
        tex->height = 0;
    }
    tex->paddr = paddr;
    LRU_use(gpu->textures, tex);
    return tex;
}

typedef struct {
    void* base;
    u32 stride;
    u32 fmt;
} AttrConfig[12];

void vtx_loader_setup(GPU* gpu, AttrConfig cfg) {
    for (int i = 0; i < 12; i++) {
        cfg[i].base = gpu->fixattrs[i];
        cfg[i].stride = 0;
        cfg[i].fmt = 0b1111;
    }
    for (int vbo = 0; vbo < 12; vbo++) {
        void* vtx =
            PTR(gpu->io.geom.attr_base * 8 + gpu->io.geom.attrbuf[vbo].offset);
        u32 stride = gpu->io.geom.attrbuf[vbo].size;
        for (int c = 0; c < gpu->io.geom.attrbuf[vbo].count; c++) {
            int attr = (gpu->io.geom.attrbuf[vbo].comp >> 4 * c) & 0xf;
            if (attr >= 0xc) {
                vtx += 4 * (attr - 0xb);
                continue;
            }
            int fmt = (gpu->io.geom.attr_format >> 4 * attr) & 0xf;

            cfg[attr].base = vtx;
            cfg[attr].stride = stride;
            cfg[attr].fmt = fmt;

            int size = (fmt >> 2) + 1;
            int type = fmt & 3;
            static const int typesize[4] = {1, 1, 2, 4};
            vtx += size * typesize[type];
        }
    }
}

void vtx_loader_imm_setup(GPU* gpu, AttrConfig cfg) {
    for (int i = 0; i < 12; i++) {
        cfg[i].base = gpu->fixattrs[i];
        cfg[i].stride = 0;
        cfg[i].fmt = 0b1111;
    }
    u32 nattrs = gpu->io.geom.vsh_num_attr + 1;
    for (int i = 0; i < nattrs; i++) {
        cfg[i].base = gpu->immattrs.d + i;
        cfg[i].stride = nattrs * sizeof(fvec);
        cfg[i].fmt = 0b1111;
    }
}

#define LOADVEC1(t)                                                            \
    ({                                                                         \
        t* attr = vtx;                                                         \
        dst[pa][0] = attr[0];                                                  \
        dst[pa][1] = dst[pa][2] = 0;                                           \
        dst[pa][3] = 1;                                                        \
    })

#define LOADVEC2(t)                                                            \
    ({                                                                         \
        t* attr = vtx;                                                         \
        dst[pa][0] = attr[0];                                                  \
        dst[pa][1] = attr[1];                                                  \
        dst[pa][2] = 0;                                                        \
        dst[pa][3] = 1;                                                        \
    })

#define LOADVEC3(t)                                                            \
    ({                                                                         \
        t* attr = vtx;                                                         \
        dst[pa][0] = attr[0];                                                  \
        dst[pa][1] = attr[1];                                                  \
        dst[pa][2] = attr[2];                                                  \
        dst[pa][3] = 1;                                                        \
    })

#define LOADVEC4(t)                                                            \
    ({                                                                         \
        t* attr = vtx;                                                         \
        dst[pa][0] = attr[0];                                                  \
        dst[pa][1] = attr[1];                                                  \
        dst[pa][2] = attr[2];                                                  \
        dst[pa][3] = attr[3];                                                  \
    })

void load_vtx(GPU* gpu, AttrConfig cfg, int i, fvec* dst) {
    u32 nattrs = gpu->io.geom.vsh_num_attr + 1;
    for (int a = 0; a < nattrs; a++) {
        void* vtx = cfg[a].base + i * cfg[a].stride;
        int pa = (gpu->io.vsh.permutation >> 4 * a) & 0xf;
        if (cfg[a].fmt == 0b1111) {
            memcpy(dst[pa], vtx, sizeof(fvec));
        } else {
            switch (cfg[a].fmt) {
                case 0b0000:
                    LOADVEC1(s8);
                    break;
                case 0b0001:
                    LOADVEC1(u8);
                    break;
                case 0b0010:
                    LOADVEC1(s16);
                    break;
                case 0b0011:
                    LOADVEC1(float);
                    break;
                case 0b0100:
                    LOADVEC2(s8);
                    break;
                case 0b0101:
                    LOADVEC2(u8);
                    break;
                case 0b0110:
                    LOADVEC2(s16);
                    break;
                case 0b0111:
                    LOADVEC2(float);
                    break;
                case 0b1000:
                    LOADVEC3(s8);
                    break;
                case 0b1001:
                    LOADVEC3(u8);
                    break;
                case 0b1010:
                    LOADVEC3(s16);
                    break;
                case 0b1011:
                    LOADVEC3(float);
                    break;
                case 0b1100:
                    LOADVEC4(s8);
                    break;
                case 0b1101:
                    LOADVEC4(u8);
                    break;
                case 0b1110:
                    LOADVEC4(s16);
                    break;
            }
        }
    }
}

void store_vtx(GPU* gpu, int i, Vertex* vbuf, fvec* src) {
    for (int o = 0; o < 7; o++) {
        for (int j = 0; j < 4; j++) {
            u8 sem = gpu->io.raster.sh_outmap[o][j];
            if (sem < 0x18) vbuf[i].semantics[sem] = src[o][j];
        }
    }
}

void init_vsh(GPU* gpu, ShaderUnit* shu) {
    shu->code = (PICAInstr*) gpu->progdata;
    shu->opdescs = (OpDesc*) gpu->opdescs;
    shu->entrypoint = gpu->io.vsh.entrypoint;
    shu->c = gpu->floatuniform;
    shu->i = gpu->io.vsh.intuniform;
    shu->b = gpu->io.vsh.booluniform;
}

void vsh_run_range(GPU* gpu, AttrConfig cfg, int srcoff, int dstoff, int count,
                   Vertex* vbuf) {
    ShaderUnit vsh;
    init_vsh(gpu, &vsh);
    for (int i = 0; i < count; i++) {
        load_vtx(gpu, cfg, srcoff + i, vsh.v);
#ifdef SHADERJIT
        gpu->vsh_runner.jitfunc(&vsh);
#else
        pica_shader_exec(&vsh);
#endif
        store_vtx(gpu, dstoff + i, vbuf, vsh.o);
    }
}

void vsh_thrd_func(GPU* gpu) {
    int id = gpu->vsh_runner.cur++;

    while (true) {
        while (!gpu->vsh_runner.thread[id].ready) {
            pthread_cond_wait(&gpu->vsh_runner.cv1, &gpu->vsh_runner.mtx);
        }
        gpu->vsh_runner.thread[id].ready = false;
        pthread_mutex_unlock(&gpu->vsh_runner.mtx);

        vsh_run_range(gpu, gpu->vsh_runner.attrcfg,
                      gpu->vsh_runner.base + gpu->vsh_runner.thread[id].off,
                      gpu->vsh_runner.thread[id].off,
                      gpu->vsh_runner.thread[id].count, gpu->vsh_runner.vbuf);

        pthread_mutex_lock(&gpu->vsh_runner.mtx);
        gpu->vsh_runner.cur++;
        pthread_cond_signal(&gpu->vsh_runner.cv2);
    }
}

void gpu_vshrunner_init(GPU* gpu) {
    gpu->vsh_runner.cur = 0;

    pthread_mutex_init(&gpu->vsh_runner.mtx, NULL);
    pthread_cond_init(&gpu->vsh_runner.cv1, NULL);
    pthread_cond_init(&gpu->vsh_runner.cv2, NULL);

    for (int i = 0; i < VSH_THREADS; i++) {
        gpu->vsh_runner.thread[i].ready = false;
        pthread_create(&gpu->vsh_runner.thread[i].thd, NULL,
                       (void*) vsh_thrd_func, gpu);
    }

    LRU_init(gpu->vshaders);
}

void dispatch_vsh(GPU* gpu, void* attrcfg, int base, int count, void* vbuf) {
#ifdef SHADERJIT
    if (gpu->sh_dirty) {
        ShaderUnit shu;
        init_vsh(gpu, &shu);
        gpu->vsh_runner.jitfunc = shaderjit_get(gpu, &shu);
        gpu->sh_dirty = false;
    }
#endif

#if VSH_THREADS == 0
    vsh_run_range(gpu, attrcfg, base, 0, count, vbuf);
#else
    if (count < VSH_THREADS) {
        vsh_run_range(gpu, attrcfg, base, 0, count, vbuf);
    } else {
        gpu->vsh_runner.attrcfg = attrcfg;
        gpu->vsh_runner.vbuf = vbuf;
        gpu->vsh_runner.base = base;

        for (int i = 0; i < VSH_THREADS; i++) {
            gpu->vsh_runner.thread[i].off = i * (count / VSH_THREADS);
            gpu->vsh_runner.thread[i].count = count / VSH_THREADS;
        }
        gpu->vsh_runner.thread[VSH_THREADS - 1].count =
            count - gpu->vsh_runner.thread[VSH_THREADS - 1].off;

        gpu->vsh_runner.cur = 0;
        for (int i = 0; i < VSH_THREADS; i++) {
            gpu->vsh_runner.thread[i].ready = true;
        }
        pthread_cond_broadcast(&gpu->vsh_runner.cv1);
        while (gpu->vsh_runner.cur < VSH_THREADS) {
            pthread_cond_wait(&gpu->vsh_runner.cv2, &gpu->vsh_runner.mtx);
        }
    }
#endif
}

void gpu_drawarrays(GPU* gpu) {
    linfo("drawing arrays nverts=%d primmode=%d", gpu->io.geom.nverts,
          gpu->io.geom.prim_config.mode);
    Vertex vbuf[gpu->io.geom.nverts];

    AttrConfig cfg;
    vtx_loader_setup(gpu, cfg);
    dispatch_vsh(gpu, cfg, gpu->io.geom.vtx_off, gpu->io.geom.nverts, vbuf);

    glBufferData(GL_ARRAY_BUFFER, sizeof vbuf, vbuf, GL_STREAM_DRAW);
    glDrawArrays(prim_mode[gpu->io.geom.prim_config.mode & 3], 0,
                 gpu->io.geom.nverts);
}

void gpu_drawelements(GPU* gpu) {
    linfo("drawing elements nverts=%d primmode=%d", gpu->io.geom.nverts,
          gpu->io.geom.prim_config.mode);

    u32 minind = 0xffff, maxind = 0;
    void* indexbuf = PTR(gpu->io.geom.attr_base * 8 + gpu->io.geom.indexbufoff);
    for (int i = 0; i < gpu->io.geom.nverts; i++) {
        int idx;
        if (gpu->io.geom.indexfmt) {
            idx = ((u16*) indexbuf)[i];
        } else {
            idx = ((u8*) indexbuf)[i];
        }
        if (idx < minind) minind = idx;
        if (idx > maxind) maxind = idx;
    }
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 gpu->io.geom.nverts * (gpu->io.geom.indexfmt ? 2 : 1),
                 indexbuf, GL_STREAM_DRAW);

    int nverts = maxind + 1 - minind;
    Vertex vbuf[nverts];

    AttrConfig cfg;
    vtx_loader_setup(gpu, cfg);
    dispatch_vsh(gpu, cfg, minind, nverts, vbuf);

    glBufferData(GL_ARRAY_BUFFER, sizeof vbuf, vbuf, GL_STREAM_DRAW);
    glDrawElementsBaseVertex(
        prim_mode[gpu->io.geom.prim_config.mode & 3], gpu->io.geom.nverts,
        gpu->io.geom.indexfmt ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE, 0,
        -minind);
}

void gpu_drawimmediate(GPU* gpu) {
    u32 nattrs = gpu->io.geom.vsh_num_attr + 1;
    u32 nverts = gpu->immattrs.size / nattrs;

    linfo("drawing immediate mode nverts=%d primmode=%d", nverts,
          gpu->io.geom.prim_config.mode);
    Vertex vbuf[nverts];

    AttrConfig cfg;
    vtx_loader_imm_setup(gpu, cfg);
    dispatch_vsh(gpu, cfg, 0, nverts, vbuf);

    glBufferData(GL_ARRAY_BUFFER, sizeof vbuf, vbuf, GL_STREAM_DRAW);
    glDrawArrays(prim_mode[gpu->io.geom.prim_config.mode & 3], 0, nverts);

    Vec_free(gpu->immattrs);
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

u32 morton_swizzle(u32 w, u32 x, u32 y) {
    u32 swizzle[8] = {
        0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15,
    };

    // textures are stored as 8x8 tiles, and within each each tile the x and y
    // coordinates are interleaved

    u32 tx = x >> 3;
    u32 fx = x & 7;
    u32 ty = y >> 3;
    u32 fy = y & 7;

    return (ty * (w >> 3) + tx) * 64 + (swizzle[fx] | swizzle[fy] << 1);
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

void* expand_nibbles(u8* src, u32 count, u8* dst) {
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

const GLint texswizzle_default[4] = {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};
const GLint texswizzle_bgr[4] = {GL_BLUE, GL_GREEN, GL_RED, GL_ALPHA};
const GLint texswizzle_lum_alpha[4] = {GL_GREEN, GL_GREEN, GL_GREEN, GL_RED};
const GLint texswizzle_luminance[4] = {GL_RED, GL_RED, GL_RED, GL_ONE};
const GLint texswizzle_alpha[4] = {GL_ZERO, GL_ZERO, GL_ZERO, GL_RED};

const GLint texswizzle_dbg_red[4] = {GL_ONE, GL_ZERO, GL_ZERO, GL_ALPHA};
const GLint texswizzle_dbg_green[4] = {GL_ZERO, GL_ONE, GL_ZERO, GL_ALPHA};
const GLint texswizzle_dbg_blue[4] = {GL_ZERO, GL_ZERO, GL_ONE, GL_ALPHA};

const int texfmtbpp[16] = {
    32, 24, 16, 16, 16, 16, 16, 8, 8, 8, 4, 4, 4, 8, 0, 0,
};
const GLint* texfmtswizzle[16] = {
    texswizzle_default,   texswizzle_bgr,       texswizzle_default,
    texswizzle_default,   texswizzle_default,   texswizzle_lum_alpha,
    texswizzle_default,   texswizzle_luminance, texswizzle_alpha,
    texswizzle_lum_alpha, texswizzle_luminance, texswizzle_alpha,
    texswizzle_default,   texswizzle_default,   texswizzle_default,
    texswizzle_default,
};

#define TEXSIZE(w, h, fmt, level)                                              \
    ((w >> level) * (h >> level) * texfmtbpp[fmt] / 8)

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

void gpu_load_texture(GPU* gpu, int id, TexUnitRegs* regs, u32 fmt) {
    if ((regs->addr << 3) < VRAM_PBASE) {
        // games setup textures with NULL sometimes
        return;
    }

    FBInfo* fb = fbcache_find(gpu, regs->addr << 3);
    glActiveTexture(GL_TEXTURE0 + id);
    if (fb) {
        glBindTexture(GL_TEXTURE_2D, fb->color_tex);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    } else {
        TexInfo* tex = texcache_load(gpu, regs->addr << 3);

        void* rawdata = PTR(tex->paddr);

        glBindTexture(GL_TEXTURE_2D, tex->tex);

        // this is not completely correct, since games often use different
        // textures with the same attributes
        // TODO: proper cache invalidation
        if (tex->width != regs->width || tex->height != regs->height ||
            tex->fmt != fmt) {
            tex->width = regs->width;
            tex->height = regs->height;
            tex->fmt = fmt;

            linfo("creating texture from %x with dims %dx%d and fmt=%d",
                  tex->paddr, tex->width, tex->height, tex->fmt);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, regs->lod.max);

            glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA,
                             texfmtswizzle[fmt]);

            // mipmap images are stored adjacent in memory and each image is
            // half the width and height of the previous one
            for (int l = regs->lod.min; l <= regs->lod.max; l++) {
                load_tex_image(rawdata, tex->width, tex->height, l, fmt);
                rawdata += TEXSIZE(tex->width, tex->height, fmt, l);
            }
        }
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    texminfilter[regs->param.min_filter |
                                 (regs->param.mipmapfilter & 1) << 1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    texmagfilter[regs->param.mag_filter]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                    texwrap[regs->param.wrap_s & 3]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                    texwrap[regs->param.wrap_t & 3]);
    float bordercolor[4];
    COPYRGBA(bordercolor, regs->border);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, bordercolor);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS,
                    (float) ((int) (regs->lod.bias << 19) >> 19) / 256);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, regs->lod.min);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, regs->lod.max);
}

void load_texenv(UberUniforms* ubuf, int i, TexEnvRegs* regs) {
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
    COPYRGBA(ubuf->tev[i].color, regs->color);
    ubuf->tev[i].rgb.scale = 1 << (regs->scale.rgb & 3);
    ubuf->tev[i].a.scale = 1 << (regs->scale.a & 3);
}

void gpu_update_gl_state(GPU* gpu) {
    gpu_update_cur_fb(gpu);

    UberUniforms ubuf;

    switch (gpu->io.raster.facecull_config & 3) {
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

    glViewport(gpu->io.raster.view_x * g_upscale,
               gpu->io.raster.view_y * g_upscale,
               2 * cvtf24(gpu->io.raster.view_w) * g_upscale,
               2 * cvtf24(gpu->io.raster.view_h) * g_upscale);

    if (gpu->io.raster.depthmap_enable) {
        float offset = cvtf24(gpu->io.raster.depthmap_offset);
        float scale = cvtf24(gpu->io.raster.depthmap_scale);
        glDepthRangef((offset - scale + 1) / 2, (offset + scale + 1) / 2);
    } else {
        glDepthRangef(0, 1);
    }

    ubuf.tex2coord = gpu->io.tex.config.tex2coord & 1;

    if (gpu->io.tex.config.tex0enable) {
        gpu_load_texture(gpu, 0, &gpu->io.tex.tex0, gpu->io.tex.tex0_fmt);
    }
    if (gpu->io.tex.config.tex1enable) {
        gpu_load_texture(gpu, 1, &gpu->io.tex.tex1, gpu->io.tex.tex1_fmt);
    }
    if (gpu->io.tex.config.tex2enable) {
        gpu_load_texture(gpu, 2, &gpu->io.tex.tex2, gpu->io.tex.tex2_fmt);
    }

    load_texenv(&ubuf, 0, &gpu->io.tex.tev0);
    load_texenv(&ubuf, 1, &gpu->io.tex.tev1);
    load_texenv(&ubuf, 2, &gpu->io.tex.tev2);
    load_texenv(&ubuf, 3, &gpu->io.tex.tev3);
    load_texenv(&ubuf, 4, &gpu->io.tex.tev4);
    load_texenv(&ubuf, 5, &gpu->io.tex.tev5);
    ubuf.tev_update_rgb = gpu->io.tex.tev_buffer.update_rgb;
    ubuf.tev_update_alpha = gpu->io.tex.tev_buffer.update_alpha;
    COPYRGBA(ubuf.tev_buffer_color, gpu->io.tex.tev5.buffer_color);

    if (gpu->io.fb.color_op.frag_mode != 0) {
        lwarn("unknown frag mode %d", gpu->io.fb.color_op.frag_mode);
    }
    if (gpu->io.fb.color_op.blend_mode == 1) {
        glDisable(GL_COLOR_LOGIC_OP);
        glEnable(GL_BLEND);
        glBlendEquationSeparate(blend_eq[gpu->io.fb.blend_func.rgb_eq & 7],
                                blend_eq[gpu->io.fb.blend_func.a_eq & 7]);
        glBlendFuncSeparate(blend_func[gpu->io.fb.blend_func.rgb_src],
                            blend_func[gpu->io.fb.blend_func.rgb_dst],
                            blend_func[gpu->io.fb.blend_func.a_src],
                            blend_func[gpu->io.fb.blend_func.a_dst]);
        glBlendColor(
            gpu->io.fb.blend_color.r / 255.f, gpu->io.fb.blend_color.g / 255.f,
            gpu->io.fb.blend_color.b / 255.f, gpu->io.fb.blend_color.a / 255.f);
    } else {
        glDisable(GL_BLEND);
        glEnable(GL_COLOR_LOGIC_OP);
        glLogicOp(logic_ops[gpu->io.fb.logic_op & 0xf]);
    }

    ubuf.alphatest = gpu->io.fb.alpha_test.enable & 1;
    ubuf.alphafunc = gpu->io.fb.alpha_test.func & 7;
    ubuf.alpharef = (float) gpu->io.fb.alpha_test.ref / 255;

    if (gpu->io.fb.stencil_test.enable) {
        glEnable(GL_STENCIL_TEST);
        if (gpu->io.fb.perms.depthbuf.write & BIT(0)) {
            glStencilMask(gpu->io.fb.stencil_test.bufmask);
        } else {
            glStencilMask(0);
        }
        glStencilFunc(compare_func[gpu->io.fb.stencil_test.func & 7],
                      gpu->io.fb.stencil_test.ref,
                      gpu->io.fb.stencil_test.mask);
        glStencilOp(stencil_op[gpu->io.fb.stencil_op.fail & 7],
                    stencil_op[gpu->io.fb.stencil_op.zfail & 7],
                    stencil_op[gpu->io.fb.stencil_op.zpass & 7]);
    } else {
        glDisable(GL_STENCIL_TEST);
    }

    if (gpu->io.fb.perms.colorbuf.write) {
        glColorMask(gpu->io.fb.color_mask.red, gpu->io.fb.color_mask.green,
                    gpu->io.fb.color_mask.blue, gpu->io.fb.color_mask.alpha);
    } else {
        glColorMask(false, false, false, false);
    }
    // you can disable writing to the depth buffer with this bit
    // instead of using the depth mask
    if (gpu->io.fb.perms.depthbuf.write & BIT(1)) {
        glDepthMask(gpu->io.fb.color_mask.depth);
    } else {
        glDepthMask(false);
    }

    // we need to always enable the depth test, since the pica can still
    // write the depth buffer even if depth testing is disabled
    glEnable(GL_DEPTH_TEST);
    if (gpu->io.fb.color_mask.depthtest) {
        glDepthFunc(compare_func[gpu->io.fb.color_mask.depthfunc & 7]);
    } else {
        glDepthFunc(GL_ALWAYS);
    }

    ubuf.numlights = (gpu->io.lighting.numlights & 7) + 1;
    for (int i = 0; i < ubuf.numlights; i++) {
        // TODO: handle light permutation
        COPYRGB(ubuf.light[i].specular0, gpu->io.lighting.light[i].specular0);
        COPYRGB(ubuf.light[i].specular1, gpu->io.lighting.light[i].specular1);
        COPYRGB(ubuf.light[i].diffuse, gpu->io.lighting.light[i].diffuse);
        COPYRGB(ubuf.light[i].ambient, gpu->io.lighting.light[i].ambient);
        ubuf.light[i].vec[0] = cvtf16(gpu->io.lighting.light[i].vec.x);
        ubuf.light[i].vec[1] = cvtf16(gpu->io.lighting.light[i].vec.y);
        ubuf.light[i].vec[2] = cvtf16(gpu->io.lighting.light[i].vec.z);
        ubuf.light[i].config = gpu->io.lighting.light[i].config;
    }
    COPYRGB(ubuf.ambient_color, gpu->io.lighting.ambient);

    glBufferData(GL_UNIFORM_BUFFER, sizeof ubuf, &ubuf, GL_STREAM_DRAW);
}
