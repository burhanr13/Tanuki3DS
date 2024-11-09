#include "gpu.h"

#include "../3ds.h"
#include "etc1.h"
#include "renderer_gl.h"
#include "shader.h"

#undef PTR
#define PTR(addr) ((void*) &gpu->mem[addr])

static const GLenum texfilter[2] = {GL_NEAREST, GL_LINEAR};
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
static const GLenum prim_mode[4] = {
    GL_TRIANGLES,
    GL_TRIANGLE_STRIP,
    GL_TRIANGLE_FAN,
    GL_TRIANGLES,
};

bool is_valid_physmem(u32 addr) {
    return (VRAM_PBASE <= addr && addr < VRAM_PBASE + VRAMSIZE) ||
           (FCRAM_PBASE <= addr && addr < FCRAM_PBASE + FCRAMSIZE);
}

void gpu_write_internalreg(GPU* gpu, u16 id, u32 param, u32 mask) {
    linfo("command %03x (0x%08x) & %08x (%f)", id, param, mask, I2F(param));
    gpu->io.w[id] &= ~mask;
    gpu->io.w[id] |= param & mask;
    switch (id) {
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
                    (*fattr)[3] = I2F(f24tof32(param >> 8));
                    gpu->curfixattr = (param & 0xff) << 16;
                    gpu->curfixi = 1;
                    break;
                }
                case 1: {
                    (*fattr)[2] = I2F(f24tof32(param >> 16 | gpu->curfixattr));
                    gpu->curfixattr = (param & MASK(16)) << 8;
                    gpu->curfixi = 2;
                    break;
                }
                case 2: {
                    (*fattr)[1] = I2F(f24tof32(param >> 24 | gpu->curfixattr));
                    (*fattr)[0] = I2F(f24tof32(param & MASK(24)));
                    gpu->curfixi = 0;
                    if (immediatemode) gpu->immattrs.size++;
                    break;
                }
            }
            break;
        }
        case GPUREG(geom.start_draw_func0):
            if (gpu->immattrs.size) {
                gpu_update_gl_state(gpu);
                gpu_drawimmediate(gpu);
            }
            break;
        case GPUREG(geom.cmdbuf.jmp[0]):
            gpu_run_command_list(gpu, gpu->io.geom.cmdbuf.addr[0] << 3,
                                 gpu->io.geom.cmdbuf.size[0] << 3);
            break;
        case GPUREG(geom.cmdbuf.jmp[1]):
            gpu_run_command_list(gpu, gpu->io.geom.cmdbuf.addr[1] << 3,
                                 gpu->io.geom.cmdbuf.size[1] << 3);
            break;
        case GPUREG(vsh.floatuniform_data[0])... GPUREG(
            vsh.floatuniform_data[7]): {
            fvec* uniform = &gpu->cst[gpu->io.vsh.floatuniform_idx];
            if (gpu->io.vsh.floatuniform_mode) {
                (*uniform)[3 - gpu->curunifi] = I2F(param);
                if (++gpu->curunifi == 4) {
                    gpu->curunifi = 0;
                    gpu->io.vsh.floatuniform_idx++;
                }
            } else {
                switch (gpu->curunifi) {
                    case 0: {
                        (*uniform)[3] = I2F(f24tof32(param >> 8));
                        gpu->curuniform = (param & 0xff) << 16;
                        gpu->curunifi = 1;
                        break;
                    }
                    case 1: {
                        (*uniform)[2] =
                            I2F(f24tof32(param >> 16 | gpu->curuniform));
                        gpu->curuniform = (param & MASK(16)) << 8;
                        gpu->curunifi = 2;
                        break;
                    }
                    case 2: {
                        (*uniform)[1] =
                            I2F(f24tof32(param >> 24 | gpu->curuniform));
                        (*uniform)[0] = I2F(f24tof32(param & MASK(24)));
                        gpu->curunifi = 0;
                        gpu->io.vsh.floatuniform_idx++;
                        break;
                    }
                }
            }
            break;
        }
        case GPUREG(vsh.codetrans_data[0])... GPUREG(vsh.codetrans_data[8]):
            gpu->progdata[gpu->io.vsh.codetrans_idx++] = param;
            break;
        case GPUREG(vsh.opdescs_data[0])... GPUREG(vsh.opdescs_data[8]):
            gpu->opdescs[gpu->io.vsh.opdescs_idx++] = param;
            break;
    }
}

void gpu_run_command_list(GPU* gpu, u32 paddr, u32 size) {
    gpu->cur_fb = NULL;

    if (!is_valid_physmem(paddr)) {
        lwarn("command list in invalid memory");
        return;
    }

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
        gpu_write_internalreg(gpu, c.id, cur[0], mask);
        cur += 2;
        if (c.incmode) c.id++;
        for (int i = 0; i < c.nparams; i++) {
            gpu_write_internalreg(gpu, c.id, *cur++, mask);
            if (c.incmode) c.id++;
        }
        if (c.nparams & 1) cur++;
    }
}

u32 f24tof32(u32 i) {
    u32 sgn = (i >> 23) & 1;
    u32 exp = (i >> 16) & MASK(7);
    u32 mantissa = i & 0xffff;
    if (exp == 0 && mantissa == 0) {
        exp = 0;
    } else if (exp == MASK(7)) {
        exp = 0xff;
    } else {
        exp += 0x40;
    }
    mantissa <<= 7;
    i = sgn << 31 | exp << 23 | mantissa;
    return i;
}

u32 f31tof32(u32 i) {
    i >>= 1;
    u32 sgn = (i >> 30) & 1;
    u32 exp = (i >> 23) & MASK(7);
    u32 mantissa = i & MASK(23);
    if (exp == 0 && mantissa == 0) {
        exp = 0;
    } else if (exp == MASK(7)) {
        exp = 0xff;
    } else {
        exp += 0x40;
    }
    i = sgn << 31 | exp << 23 | mantissa;
    return i;
}

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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gpu->cur_fb->width * SCALE,
                     gpu->cur_fb->height * SCALE, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     NULL);
        glBindTexture(GL_TEXTURE_2D, gpu->cur_fb->depth_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8,
                     gpu->cur_fb->width * SCALE, gpu->cur_fb->height * SCALE, 0,
                     GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, gpu->cur_fb->color_tex, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                               GL_TEXTURE_2D, gpu->cur_fb->depth_tex, 0);

        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
}

void gpu_display_transfer(GPU* gpu, u32 paddr, int yoff, bool top) {
    FBInfo* fb = NULL;
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

    linfo("display transfer fb at %x to %s", paddr, top ? "top" : "bot");

    GLuint dst = top ? gpu->gl.textop : gpu->gl.texbot;
    glBindTexture(GL_TEXTURE_2D, dst);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb->fbo);
    u32 scwidth = top ? SCREEN_WIDTH : SCREEN_WIDTH_BOT;
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0,
                     fb->height - scwidth + yoff + yoffsrc,
                     SCREEN_HEIGHT * SCALE, scwidth * SCALE, 0);
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
            glClear(GL_DEPTH_BUFFER_BIT);
            linfo("cleared depth buffer at %x of fb %d with value %x", paddr, i,
                  color);
            return;
        }
    }
}

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

u32 morton_swizzle(u32 h, u32 w, u32 x, u32 y) {
    u32 swizzle[8] = {
        0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15,
    };

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
        t* pixels = malloc(sizeof(t) * tex->width * tex->height);              \
                                                                               \
        for (int x = 0; x < tex->width; x++) {                                 \
            for (int y = 0; y < tex->height; y++) {                            \
                pixels[y * tex->width + x] =                                   \
                    data[morton_swizzle(tex->height, tex->width, x, y)];       \
            }                                                                  \
        }                                                                      \
                                                                               \
        glTexImage2D(GL_TEXTURE_2D, 0, glfmt, tex->width, tex->height, 0,      \
                     glfmt, gltype, pixels);                                   \
        free(pixels);                                                          \
    })

void* expand_nibbles(u8* src, u32 count) {
    u8* dst = malloc(count);
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
const GLint texswizzle_lum_alpha[4] = {GL_GREEN, GL_GREEN, GL_GREEN, GL_RED};
const GLint texswizzle_luminance[4] = {GL_RED, GL_RED, GL_RED, GL_ONE};
const GLint texswizzle_alpha[4] = {GL_ZERO, GL_ZERO, GL_ZERO, GL_RED};

const GLint texswizzle_dbg_red[4] = {GL_ONE, GL_ZERO, GL_ZERO, GL_ALPHA};
const GLint texswizzle_dbg_green[4] = {GL_ZERO, GL_ONE, GL_ZERO, GL_ALPHA};
const GLint texswizzle_dbg_blue[4] = {GL_ZERO, GL_ZERO, GL_ONE, GL_ALPHA};

void gpu_load_texture(GPU* gpu, int id, TexUnitRegs* regs, u32 fmt) {
    if (!is_valid_physmem(regs->addr << 3)) {
        lwarn("reading textures from invalid memory");
        return;
    }

    FBInfo* fb = fbcache_find(gpu, regs->addr << 3);
    glActiveTexture(GL_TEXTURE0 + id);
    if (fb) {
        glBindTexture(GL_TEXTURE_2D, fb->color_tex);
    } else {
        TexInfo* tex = texcache_load(gpu, regs->addr << 3);

        void* rawdata = PTR(tex->paddr);

        glBindTexture(GL_TEXTURE_2D, tex->tex);

        if (tex->width != regs->width || tex->height != regs->height ||
            tex->fmt != fmt) {
            tex->width = regs->width;
            tex->height = regs->height;
            tex->fmt = fmt;

            linfo("creating texture from %x with dims %dx%d and fmt=%d",
                  tex->paddr, tex->width, tex->height, tex->fmt);

            glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA,
                             texswizzle_default);

            switch (fmt) {
                case 0:
                    LOAD_TEX(u32, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8);
                    break;
                case 1:
                    LOAD_TEX(u24, GL_RGB, GL_UNSIGNED_BYTE);
                    break;
                case 2:
                    LOAD_TEX(u16, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1);
                    break;
                case 3:
                    LOAD_TEX(u16, GL_RGB, GL_UNSIGNED_SHORT_5_6_5);
                    break;
                case 4:
                    LOAD_TEX(u16, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4);
                    break;
                case 5:
                    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA,
                                     texswizzle_lum_alpha);
                    LOAD_TEX(u16, GL_RG, GL_UNSIGNED_BYTE);
                    break;
                case 7:
                    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA,
                                     texswizzle_luminance);
                    LOAD_TEX(u8, GL_RED, GL_UNSIGNED_BYTE);
                    break;
                case 8:
                    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA,
                                     texswizzle_alpha);
                    LOAD_TEX(u8, GL_RED, GL_UNSIGNED_BYTE);
                    break;
                case 9:
                    rawdata =
                        expand_nibbles(rawdata, 2 * tex->width * tex->height);
                    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA,
                                     texswizzle_lum_alpha);
                    LOAD_TEX(u16, GL_RG, GL_UNSIGNED_BYTE);
                    free(rawdata);
                    break;
                case 10:
                    rawdata = expand_nibbles(rawdata, tex->width * tex->height);
                    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA,
                                     texswizzle_luminance);
                    LOAD_TEX(u8, GL_RED, GL_UNSIGNED_BYTE);
                    free(rawdata);
                    break;
                case 11:
                    rawdata = expand_nibbles(rawdata, tex->width * tex->height);
                    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA,
                                     texswizzle_alpha);
                    LOAD_TEX(u8, GL_RED, GL_UNSIGNED_BYTE);
                    free(rawdata);
                    break;
                case 12: {
                    u8* dec = etc1_decompress_texture(tex->width, tex->height,
                                                      rawdata);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex->width,
                                 tex->height, 0, GL_RGB, GL_UNSIGNED_BYTE, dec);
                    free(dec);
                    break;
                }
                case 13: {
                    u8* dec = etc1a4_decompress_texture(tex->width, tex->height,
                                                        rawdata);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex->width,
                                 tex->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                                 dec);
                    free(dec);
                    break;
                }
                default:
                    lwarn("unknown texture format %d", fmt);
            }
        }
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    texfilter[regs->param.min_filter]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    texfilter[regs->param.mag_filter]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                    texwrap[regs->param.wrap_s & 3]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                    texwrap[regs->param.wrap_t & 3]);
}

void load_vtx(GPU* gpu, int i) {
    memcpy(gpu->in, gpu->fixattrs, sizeof gpu->in);
    for (int vbo = 0; vbo < 12; vbo++) {
        if (!is_valid_physmem(gpu->io.geom.attr_base * 8 +
                              gpu->io.geom.attrbuf[vbo].offset +
                              i * gpu->io.geom.attrbuf[vbo].size)) {
            lwarn("vbo in invalid memory");
            continue;
        }
        void* vtx =
            PTR(gpu->io.geom.attr_base * 8 + gpu->io.geom.attrbuf[vbo].offset +
                i * gpu->io.geom.attrbuf[vbo].size);
        for (int c = 0; c < gpu->io.geom.attrbuf[vbo].count; c++) {
            int attr = (gpu->io.geom.attrbuf[vbo].comp >> 4 * c) & 0xf;
            if (attr >= 0xc) {
                vtx += 4 * (attr - 0xb);
                continue;
            }
            int type = (gpu->io.geom.attr_format >> 4 * attr) & 0xf;
            attr = (gpu->io.vsh.permutation >> 4 * attr) & 0xf;
            int size = (type >> 2) + 1;
            type &= 3;
            switch (type) {
                case 0: {
                    s8* ptr = vtx;
                    for (int k = 0; k < size; k++) {
                        gpu->in[attr][k] = *ptr++;
                    }
                    vtx = ptr;
                    break;
                }
                case 1: {
                    u8* ptr = vtx;
                    for (int k = 0; k < size; k++) {
                        gpu->in[attr][k] = *ptr++;
                    }
                    vtx = ptr;
                    break;
                }
                case 2: {
                    s16* ptr = vtx;
                    for (int k = 0; k < size; k++) {
                        gpu->in[attr][k] = *ptr++;
                    }
                    vtx = ptr;
                    break;
                }
                case 3: {
                    float* ptr = vtx;
                    for (int k = 0; k < size; k++) {
                        gpu->in[attr][k] = *ptr++;
                    }
                    vtx = ptr;
                    break;
                }
            }
        }
    }
}

void load_vtx_imm(GPU* gpu, int i) {
    u32 nattrs = gpu->io.geom.vsh_num_attr + 1;
    memcpy(gpu->in, &gpu->immattrs.d[i * nattrs], nattrs * sizeof(fvec));
}

void store_vtx(GPU* gpu, int i, Vertex* vbuf) {
    for (int o = 0; o < 7; o++) {
        for (int j = 0; j < 4; j++) {
            u8 sem = gpu->io.raster.sh_outmap[o][j];
            if (sem < 0x18) vbuf[i].semantics[sem] = gpu->out[o][j];
        }
    }
}

void gpu_drawarrays(GPU* gpu) {
    linfo("drawing arrays nverts=%d primmode=%d", gpu->io.geom.nverts,
          gpu->io.geom.prim_config.mode);
    Vertex vbuf[gpu->io.geom.nverts];
    for (int i = 0; i < gpu->io.geom.nverts; i++) {
        load_vtx(gpu, i + gpu->io.geom.vtx_off);
        exec_vshader(gpu);
        store_vtx(gpu, i, vbuf);
    }
    glBufferData(GL_ARRAY_BUFFER, gpu->io.geom.nverts * sizeof(Vertex), vbuf,
                 GL_STREAM_DRAW);
    glDrawArrays(prim_mode[gpu->io.geom.prim_config.mode & 3], 0,
                 gpu->io.geom.nverts);
}

void gpu_drawelements(GPU* gpu) {
    linfo("drawing elements nverts=%d primmode=%d", gpu->io.geom.nverts,
          gpu->io.geom.prim_config.mode);

    if (!is_valid_physmem(gpu->io.geom.attr_base * 8 +
                          gpu->io.geom.indexbufoff)) {
        lwarn("reading index buffer from invalid memory");
        return;
    }

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

    Vertex vbuf[maxind + 1 - minind];
    for (int i = minind; i <= maxind; i++) {
        load_vtx(gpu, i - minind);
        exec_vshader(gpu);
        store_vtx(gpu, i - minind, vbuf);
    }

    glBufferData(GL_ARRAY_BUFFER, (maxind + 1 - minind) * sizeof(Vertex), vbuf,
                 GL_STREAM_DRAW);
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
    for (int i = 0; i < nverts; i++) {
        load_vtx_imm(gpu, i);
        exec_vshader(gpu);
        store_vtx(gpu, i, vbuf);
    }
    glBufferData(GL_ARRAY_BUFFER, nverts * sizeof(Vertex), vbuf,
                 GL_STREAM_DRAW);
    glDrawArrays(prim_mode[gpu->io.geom.prim_config.mode & 3], 0, nverts);

    Vec_free(gpu->immattrs);
}

void gpu_update_gl_state(GPU* gpu) {
    gpu_update_cur_fb(gpu);

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

    glViewport(gpu->io.raster.view_x * SCALE, gpu->io.raster.view_y * SCALE,
               2 * I2F(f24tof32(gpu->io.raster.view_w)) * SCALE,
               2 * I2F(f24tof32(gpu->io.raster.view_h)) * SCALE);

    if (gpu->io.raster.depthmap_enable) {
        float offset = I2F(f24tof32(gpu->io.raster.depthmap_offset));
        float scale = I2F(f24tof32(gpu->io.raster.depthmap_scale));
        glDepthRangef((offset - scale + 1) / 2, (offset + scale + 1) / 2);
    } else {
        glDepthRangef(0, 1);
    }

    if (gpu->io.tex.texunit_cfg & 1) {
        glUniform1i(gpu->gl.uniforms.tex0enable, true);
        gpu_load_texture(gpu, 0, &gpu->io.tex.tex0, gpu->io.tex.tex0_fmt);
    } else {
        glUniform1i(gpu->gl.uniforms.tex0enable, false);
    }

    if (gpu->io.fb.color_op.frag_mode != 0) {
        lwarn("using frag op %d", gpu->io.fb.color_op.frag_mode);
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

    glColorMask(gpu->io.fb.color_mask.red, gpu->io.fb.color_mask.green,
                gpu->io.fb.color_mask.blue, gpu->io.fb.color_mask.alpha);
    glDepthMask(gpu->io.fb.color_mask.depth);
    glEnable(GL_DEPTH_TEST);
    if (gpu->io.fb.color_mask.depthtest) {
        glDepthFunc(compare_func[gpu->io.fb.color_mask.depthfunc & 7]);
    } else {
        glDepthFunc(GL_ALWAYS);
    }
}
