#include "gpu.h"

#include <xxh3.h>

#include "3ds.h"
#include "emulator.h"
#include "kernel/memory.h"

#include "etc1.h"
#include "renderer_gl.h"
#include "shader.h"
#include "shaderdec.h"
#include "shadergen.h"

#include "gpuptr.inc"

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
    return (VRAM_PBASE <= addr && addr < VRAM_PBASE + VRAM_SIZE) ||
           (FCRAM_PBASE <= addr && addr < FCRAM_PBASE + FCRAM_SIZE);
}

bool is_vram_addr(u32 addr) {
    return VRAM_PBASE <= addr && addr < VRAM_PBASE + VRAM_SIZE;
}

void gpu_init(GPU* gpu) {
    LRU_init(gpu->fbs);
    // ensure this is pointing to something
    gpu->curfb = &gpu->fbs.root;
    LRU_init(gpu->textures);
    LRU_init(gpu->vshaders_sw);
    LRU_init(gpu->vshaders_hw);
    LRU_init(gpu->fshaders);

    gpu_vshrunner_init(gpu);
}

void gpu_destroy(GPU* gpu) {
    shaderjit_free_all(gpu);

    gpu_vshrunner_destroy(gpu);
}

void gpu_write_internalreg(GPU* gpu, u16 id, u32 param, u32 mask) {
    if (id >= GPUREG_MAX) {
        lerror("out of bounds gpu reg");
        return;
    }
    linfo("command %03x (0x%08x) & %08x (%f)", id, param, mask, I2F(param));
    gpu->regs.w[id] &= ~mask;
    gpu->regs.w[id] |= param & mask;
    switch (id) {
        case GPUREG(geom.cmdbuf.jmp[0]):
            gpu_run_command_list(gpu, gpu->regs.geom.cmdbuf.addr[0] << 3,
                                 gpu->regs.geom.cmdbuf.size[0] << 3);
            return;
        case GPUREG(geom.cmdbuf.jmp[1]):
            gpu_run_command_list(gpu, gpu->regs.geom.cmdbuf.addr[1] << 3,
                                 gpu->regs.geom.cmdbuf.size[1] << 3);
            return;
        case GPUREG(geom.drawarrays):
            gpu_drawarrays(gpu);
            break;
        case GPUREG(geom.drawelements):
            gpu_drawelements(gpu);
            break;
        case GPUREG(geom.fixattr_data[0])... GPUREG(geom.fixattr_data[2]): {
            fvec4* fattr;
            bool immediatemode = false;
            if (gpu->regs.geom.fixattr_idx == 0xf) {
                Vec_grow(gpu->immattrs);
                fattr = &gpu->immattrs.d[gpu->immattrs.size];
                immediatemode = true;
            } else {
                fattr = &gpu->fixattrs[gpu->regs.geom.fixattr_idx];
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
                gpu_drawimmediate(gpu);
            }
            break;
        case GPUREG(gsh.floatuniform_data[0])... GPUREG(
            gsh.floatuniform_data[7]): {
            u32 idx = gpu->regs.gsh.floatuniform_idx;
            if (idx >= 96) {
                linfo("writing to out of bound uniform");
                break;
            }
            fvec4* uniform = &gpu->gsh.floatuniform[idx];
            if (gpu->regs.gsh.floatuniform_mode) {
                (*uniform)[3 - gpu->gsh.curunifi] = I2F(param);
                if (++gpu->gsh.curunifi == 4) {
                    gpu->gsh.curunifi = 0;
                    gpu->regs.gsh.floatuniform_idx++;
                }
            } else {
                switch (gpu->gsh.curunifi) {
                    case 0: {
                        (*uniform)[3] = cvtf24(param >> 8);
                        gpu->gsh.curuniform = (param & 0xff) << 16;
                        gpu->gsh.curunifi = 1;
                        break;
                    }
                    case 1: {
                        (*uniform)[2] =
                            cvtf24(param >> 16 | gpu->gsh.curuniform);
                        gpu->gsh.curuniform = (param & MASK(16)) << 8;
                        gpu->gsh.curunifi = 2;
                        break;
                    }
                    case 2: {
                        (*uniform)[1] =
                            cvtf24(param >> 24 | gpu->gsh.curuniform);
                        (*uniform)[0] = cvtf24(param & MASK(24));
                        gpu->gsh.curunifi = 0;
                        gpu->regs.gsh.floatuniform_idx++;
                        break;
                    }
                }
            }
            break;
        }
        case GPUREG(vsh.floatuniform_data[0])... GPUREG(
            vsh.floatuniform_data[7]): {
            gpu->vsh_uniform_dirty = true;
            u32 idx = gpu->regs.vsh.floatuniform_idx;
            if (idx >= 96) {
                linfo("writing to out of bound uniform");
                break;
            }
            fvec4* uniform = &gpu->vsh.floatuniform[idx];
            if (gpu->regs.vsh.floatuniform_mode) {
                (*uniform)[3 - gpu->vsh.curunifi] = I2F(param);
                if (++gpu->vsh.curunifi == 4) {
                    gpu->vsh.curunifi = 0;
                    gpu->regs.vsh.floatuniform_idx++;
                }
            } else {
                switch (gpu->vsh.curunifi) {
                    case 0: {
                        (*uniform)[3] = cvtf24(param >> 8);
                        gpu->vsh.curuniform = (param & 0xff) << 16;
                        gpu->vsh.curunifi = 1;
                        break;
                    }
                    case 1: {
                        (*uniform)[2] =
                            cvtf24(param >> 16 | gpu->vsh.curuniform);
                        gpu->vsh.curuniform = (param & MASK(16)) << 8;
                        gpu->vsh.curunifi = 2;
                        break;
                    }
                    case 2: {
                        (*uniform)[1] =
                            cvtf24(param >> 24 | gpu->vsh.curuniform);
                        (*uniform)[0] = cvtf24(param & MASK(24));
                        gpu->vsh.curunifi = 0;
                        gpu->regs.vsh.floatuniform_idx++;
                        break;
                    }
                }
            }
            break;
        }
        case GPUREG(vsh.intuniform[0])... GPUREG(vsh.intuniform[3]):
        case GPUREG(vsh.booluniform):
            gpu->vsh_uniform_dirty = true;
            break;
        case GPUREG(gsh.entrypoint):
            gpu->gsh.code_dirty = true;
            break;
        case GPUREG(gsh.codetrans_data[0])... GPUREG(gsh.codetrans_data[8]):
            gpu->gsh.code_dirty = true;
            gpu->gsh
                .progdata[gpu->regs.gsh.codetrans_idx++ % SHADER_CODE_SIZE] =
                param;
            break;
        case GPUREG(gsh.opdescs_data[0])... GPUREG(gsh.opdescs_data[8]):
            gpu->gsh.code_dirty = true;
            gpu->gsh.opdescs[gpu->regs.gsh.opdescs_idx++ % SHADER_OPDESC_SIZE] =
                param;
            break;
        case GPUREG(vsh.entrypoint):
        case GPUREG(raster.sh_outmap[0])... GPUREG(raster.sh_outmap[6]):
            // entrypoint and outmap both affect the decompiled vs
            gpu->vsh.code_dirty = true;
            break;
        case GPUREG(vsh.codetrans_data[0])... GPUREG(vsh.codetrans_data[8]):
            gpu->vsh.code_dirty = true;
            gpu->vsh
                .progdata[gpu->regs.vsh.codetrans_idx++ % SHADER_CODE_SIZE] =
                param;
            break;
        case GPUREG(vsh.opdescs_data[0])... GPUREG(vsh.opdescs_data[8]):
            gpu->vsh.code_dirty = true;
            gpu->vsh.opdescs[gpu->regs.vsh.opdescs_idx++ % SHADER_OPDESC_SIZE] =
                param;
            break;
    }
}

void gpu_reset_needs_rehesh(GPU* gpu) {
    // this is called every time gsp starts a new command list, since
    // the cpu cant modify a texture within a command list, so no need to rehash
    // textures more often than that
    for (int i = 0; i < TEX_MAX; i++) {
        gpu->textures.d[i].needs_rehash = true;
    }
}

void gpu_run_command_list(GPU* gpu, u32 paddr, u32 size) {

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

        gpu_write_internalreg(gpu, c.id, cur[0], mask);
        cur += 2;
        if (c.incmode) c.id++;
        for (int i = 0; i < c.nparams; i++) {
            gpu_write_internalreg(gpu, c.id, *cur++, mask);
            if (c.incmode) c.id++;
        }
        // each command must be 8 byte aligned
        if (c.nparams & 1) cur++;
    }
}

// searches the framebuffer cache and return nullptr if not found
FBInfo* fbcache_find(GPU* gpu, u32 color_paddr) {
    FBInfo* newfb = nullptr;
    for (int i = 0; i < FB_MAX; i++) {
        if (gpu->fbs.d[i].color_paddr == color_paddr) {
            newfb = &gpu->fbs.d[i];
            break;
        }
    }
    if (newfb) LRU_use(gpu->fbs, newfb);
    return newfb;
}

// searches the framebuffer cache and return nullptr if not found
FBInfo* fbcache_find_within(GPU* gpu, u32 color_paddr) {
    FBInfo* newfb = nullptr;
    for (int i = 0; i < FB_MAX; i++) {
        if (gpu->fbs.d[i].color_paddr <= color_paddr &&
            color_paddr < gpu->fbs.d[i].color_paddr +
                              gpu->fbs.d[i].width * gpu->fbs.d[i].height *
                                  gpu->fbs.d[i].color_Bpp) {
            newfb = &gpu->fbs.d[i];
            break;
        }
    }
    if (newfb) LRU_use(gpu->fbs, newfb);
    return newfb;
}

TexInfo* texcache_find_within(GPU* gpu, u32 paddr) {
    TexInfo* tex = nullptr;
    for (int i = 0; i < TEX_MAX; i++) {
        if (gpu->textures.d[i].paddr <= paddr &&
            paddr < gpu->textures.d[i].paddr + gpu->textures.d[i].size) {
            tex = &gpu->textures.d[i];
            break;
        }
    }
    return tex;
}

void gpu_display_transfer(GPU* gpu, u32 paddr, int yoff, bool scalex,
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

void gpu_render_lcd_fb(GPU* gpu, u32 paddr, u32 fmt, int screenid) {
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

void gpu_texture_copy(GPU* gpu, u32 srcpaddr, u32 dstpaddr, u32 size,
                      u32 srcpitch, u32 srcgap, u32 dstpitch, u32 dstgap) {

    if (!srcpitch || !dstpitch) return; // why

    auto srcfb = fbcache_find_within(gpu, srcpaddr);
    auto dsttex = texcache_find_within(gpu, dstpaddr);

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

void gpu_clear_fb(GPU* gpu, u32 paddr, u32 endPaddr, u32 value, u32 datasz) {
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

// the first wall of defense for texture cache invalidation
void gpu_invalidate_range(GPU* gpu, u32 paddr, u32 len) {
    linfo("invalidating cache at %08x-%08x", paddr, paddr + len);

    // probably should optimize this at some point to not be linear
    for (int i = 0; i < TEX_MAX; i++) {
        auto t = &gpu->textures.d[i];
        if ((t->paddr <= paddr && paddr < t->paddr + t->size) ||
            (t->paddr < paddr + len && paddr + len <= t->paddr + t->size)) {
            LRU_remove(gpu->textures, t);
        }
    }
}

void update_cur_fb(GPU* gpu) {
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
const GLint texswizzle_zero[4] = {GL_ZERO, GL_ZERO, GL_ZERO, GL_ZERO};

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

void create_texture(GPU* gpu, TexInfo* tex, TexUnitRegs* regs) {
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

u64 get_texture_hash(void* tex, u32 size) {
    if (!ctremu.hashTextures) return 0;
    // maybe we might want to do something different later but rn this is fine
    return XXH3_64bits(tex, size);
}

void load_texture(GPU* gpu, int id, TexUnitRegs* regs, u32 fmt) {
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
            tex->hash = get_texture_hash(data, tex->size);
            tex->needs_rehash = false;

            create_texture(gpu, tex, regs);
        } else if (tex->needs_rehash) {
            void* data = PTR(tex->paddr);
            u64 hash = get_texture_hash(data, tex->size);
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
    FBInfo* fb = fbcache_find(gpu, tex->paddr);
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

void load_texenv(UberUniforms* ubuf, FragUniforms* fbuf, int i,
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

void update_gl_state(GPU* gpu) {

    update_cur_fb(gpu);

    // ensure unused entries are 0 so the hashing is consistent
    UberUniforms ubuf = {};

    FragUniforms fbuf;

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

    if (gpu->regs.raster.depthmap_enable) {
        float offset = cvtf24(gpu->regs.raster.depthmap_offset);
        float scale = cvtf24(gpu->regs.raster.depthmap_scale);
        // pica near plane is -1 and farplane is 0
        glDepthRange(offset - scale, offset);
    } else {
        // default depth range maps -1 -> 1 and 0 -> 0
        glDepthRange(1, 0);
    }

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

    load_texenv(&ubuf, &fbuf, 0, &gpu->regs.tex.tev0);
    load_texenv(&ubuf, &fbuf, 1, &gpu->regs.tex.tev1);
    load_texenv(&ubuf, &fbuf, 2, &gpu->regs.tex.tev2);
    load_texenv(&ubuf, &fbuf, 3, &gpu->regs.tex.tev3);
    load_texenv(&ubuf, &fbuf, 4, &gpu->regs.tex.tev4);
    load_texenv(&ubuf, &fbuf, 5, &gpu->regs.tex.tev5);
    ubuf.tev_update_rgb = gpu->regs.tex.tev_buffer.update_rgb;
    ubuf.tev_update_alpha = gpu->regs.tex.tev_buffer.update_alpha;
    COPYRGBA(fbuf.tev_buffer_color, gpu->regs.tex.tev5.buffer_color);

    if (gpu->regs.fb.color_op.frag_mode != 0) {
        return; // shadows or gas, ignore these for now
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

    ubuf.alphatest = gpu->regs.fb.alpha_test.enable;
    ubuf.alphafunc = gpu->regs.fb.alpha_test.func;
    fbuf.alpharef = (float) gpu->regs.fb.alpha_test.ref / 255;

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

    // we need to always enable the depth test, since the pica can still
    // write the depth buffer even if depth testing is disabled
    glEnable(GL_DEPTH_TEST);
    if (gpu->regs.fb.color_mask.depthtest) {
        glDepthFunc(compare_func[gpu->regs.fb.color_mask.depthfunc]);
    } else {
        glDepthFunc(GL_ALWAYS);
    }

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

    GLuint vs;
    if (ctremu.hwvshaders && !gpu->regs.geom.config.use_gsh) {
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
        if (gpu->vsh.code_dirty) {
            vs = shader_dec_get(gpu);
        } else {
            vs = LRU_mru(gpu->vshaders_hw)->vs;
        }

        glBindVertexArray(gpu->gl.gpu_vao_hw);
    } else {
        vs = gpu->gl.gpu_vs;
        glBindVertexArray(gpu->gl.gpu_vao_sw);
    }

    // todo: do similar dirty checking for the fs
    glBindBuffer(GL_UNIFORM_BUFFER, gpu->gl.frag_ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof fbuf, &fbuf, GL_STREAM_DRAW);

    GLuint fs;
    if (ctremu.ubershader) {
        glBindBuffer(GL_UNIFORM_BUFFER, gpu->gl.uber_ubo);
        glBufferData(GL_UNIFORM_BUFFER, sizeof ubuf, &ubuf, GL_STREAM_DRAW);
        fs = gpu->gl.gpu_uberfs;
    } else {
        fs = shader_gen_get(gpu, &ubuf);
    }

    gpu_gl_load_prog(&gpu->gl, vs, fs);
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
        void* vtx = PTR(gpu->regs.geom.attr_base * 8 +
                        gpu->regs.geom.attrbuf[vbo].offset);
        u32 stride = gpu->regs.geom.attrbuf[vbo].size;
        for (int c = 0; c < gpu->regs.geom.attrbuf[vbo].count; c++) {
            int attr = (gpu->regs.geom.attrbuf[vbo].comp >> 4 * c) & 0xf;
            if (attr >= 0xc) {
                vtx += 4 * (attr - 0xb);
                continue;
            }
            int fmt = (gpu->regs.geom.attr_format >> 4 * attr) & 0xf;

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
    u32 nattrs = gpu->regs.geom.vsh_num_attr + 1;
    for (int i = 0; i < nattrs; i++) {
        cfg[i].base = gpu->immattrs.d + i;
        cfg[i].stride = nattrs * sizeof(fvec4);
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

void load_vtx(GPU* gpu, AttrConfig cfg, int i, fvec4* dst) {
    u32 nattrs = gpu->regs.geom.vsh_num_attr + 1;
    for (int a = 0; a < nattrs; a++) {
        void* vtx = cfg[a].base + i * cfg[a].stride;
        int pa = (gpu->regs.vsh.permutation >> 4 * a) & 0xf;
        if (cfg[a].fmt == 0b1111) {
            memcpy(dst[pa], vtx, sizeof(fvec4));
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

void store_vtx(GPU* gpu, int i, Vertex* vbuf, fvec4* src) {
    for (int o = 0; o < 7; o++) {
        for (int j = 0; j < 4; j++) {
            u8 sem = gpu->regs.raster.sh_outmap[o][j];
            if (sem < 0x18) vbuf[i].semantics[sem] = src[o][j];
        }
    }
}

void init_vsh(GPU* gpu, ShaderUnit* shu) {
    shu->code = (PICAInstr*) gpu->vsh.progdata;
    shu->opdescs = (OpDesc*) gpu->vsh.opdescs;
    shu->entrypoint = gpu->regs.vsh.entrypoint;
    shu->c = gpu->vsh.floatuniform;
    shu->i = gpu->regs.vsh.intuniform;
    shu->b = gpu->regs.vsh.booluniform;
}

void init_gsh(GPU* gpu, ShaderUnit* shu) {
    shu->code = (PICAInstr*) gpu->gsh.progdata;
    shu->opdescs = (OpDesc*) gpu->gsh.opdescs;
    shu->entrypoint = gpu->regs.gsh.entrypoint;
    shu->c = gpu->gsh.floatuniform;
    shu->i = gpu->regs.gsh.intuniform;
    shu->b = gpu->regs.gsh.booluniform;
    Vec_init(shu->gsh.outvtx);
}

void vsh_run_range(GPU* gpu, AttrConfig cfg, int srcoff, int dstoff, int count,
                   fvec4 (*vbuf)[16]) {
    ShaderUnit vsh;
    init_vsh(gpu, &vsh);
    for (int i = 0; i < count; i++) {
        load_vtx(gpu, cfg, srcoff + i, vsh.v);
        gpu->vsh_runner.shaderfunc(&vsh);
        memcpy(vbuf[dstoff + i], vsh.o, sizeof vsh.o);
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

        if (gpu->vsh_runner.die) return;

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

    pthread_mutex_init(&gpu->vsh_runner.mtx, nullptr);
    pthread_cond_init(&gpu->vsh_runner.cv1, nullptr);
    pthread_cond_init(&gpu->vsh_runner.cv2, nullptr);

    for (int i = 0; i < ctremu.vshthreads; i++) {
        gpu->vsh_runner.thread[i].ready = false;
        pthread_create(&gpu->vsh_runner.thread[i].thd, nullptr,
                       (void*) vsh_thrd_func, gpu);
    }
}

void gpu_vshrunner_destroy(GPU* gpu) {
    gpu->vsh_runner.die = true;
    for (int i = 0; i < ctremu.vshthreads; i++) {
        gpu->vsh_runner.thread[i].ready = true;
    }
    pthread_cond_broadcast(&gpu->vsh_runner.cv1);
    pthread_mutex_unlock(&gpu->vsh_runner.mtx);
    for (int i = 0; i < ctremu.vshthreads; i++) {
        pthread_join(gpu->vsh_runner.thread[i].thd, nullptr);
    }
    pthread_mutex_destroy(&gpu->vsh_runner.mtx);
    pthread_cond_destroy(&gpu->vsh_runner.cv1);
    pthread_cond_destroy(&gpu->vsh_runner.cv2);
}

void dispatch_vsh(GPU* gpu, void* attrcfg, int base, int count, void* vbuf) {
    if (ctremu.shaderjit) {
        if (gpu->vsh.code_dirty) {
            ShaderUnit shu;
            init_vsh(gpu, &shu);
            gpu->vsh_runner.shaderfunc = shaderjit_get(gpu, &shu);
            gpu->vsh.code_dirty = false;
        }
    } else {
        gpu->vsh_runner.shaderfunc = pica_shader_exec;
    }

    if (count < ctremu.vshthreads || ctremu.vshthreads < 2) {
        vsh_run_range(gpu, attrcfg, base, 0, count, vbuf);
    } else {
        gpu->vsh_runner.attrcfg = attrcfg;
        gpu->vsh_runner.vbuf = vbuf;
        gpu->vsh_runner.base = base;

        for (int i = 0; i < ctremu.vshthreads; i++) {
            gpu->vsh_runner.thread[i].off = i * (count / ctremu.vshthreads);
            gpu->vsh_runner.thread[i].count = count / ctremu.vshthreads;
        }
        gpu->vsh_runner.thread[ctremu.vshthreads - 1].count =
            count - gpu->vsh_runner.thread[ctremu.vshthreads - 1].off;

        gpu->vsh_runner.cur = 0;
        for (int i = 0; i < ctremu.vshthreads; i++) {
            gpu->vsh_runner.thread[i].ready = true;
        }
        pthread_cond_broadcast(&gpu->vsh_runner.cv1);
        while (gpu->vsh_runner.cur < ctremu.vshthreads) {
            pthread_cond_wait(&gpu->vsh_runner.cv2, &gpu->vsh_runner.mtx);
        }
    }
}

void run_vsh(GPU* gpu, int start, int num, fvec4 (*out)[16]) {
    AttrConfig cfg;
    vtx_loader_setup(gpu, cfg);
    dispatch_vsh(gpu, cfg, start, num, out);
}

static const GLuint attrtypes[] = {
    GL_BYTE,
    GL_UNSIGNED_BYTE,
    GL_SHORT,
    GL_FLOAT,
};

void setup_fixattrs_hw(GPU* gpu) {
    for (int i = 0; i < 12; i++) {
        if (gpu->regs.geom.fixed_attr_mask & BIT(i)) {
            int attr = (gpu->regs.vsh.permutation >> 4 * i) & 0xf;
            glVertexAttrib4fv(attr, gpu->fixattrs[i]);
            glDisableVertexAttribArray(attr);
        }
    }
}

void setup_vbos_hw(GPU* gpu, int start, int num) {
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

void draw_gsh_out(GPU* gpu, ShaderUnit* gsh) {
    Vertex vbuf[gsh->gsh.outvtx.size];
    for (int i = 0; i < gsh->gsh.outvtx.size; i++) {
        store_vtx(gpu, i, vbuf, gsh->gsh.outvtx.d[i]);
    }
    glBufferData(GL_ARRAY_BUFFER, sizeof vbuf, vbuf, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, gsh->gsh.outvtx.size);
}

static const GLenum prim_mode[4] = {
    GL_TRIANGLES,
    GL_TRIANGLE_STRIP,
    GL_TRIANGLE_FAN,
    GL_TRIANGLES,
};

void gpu_drawarrays(GPU* gpu) {
    linfo("drawing arrays nverts=%d primmode=%d", gpu->regs.geom.nverts,
          gpu->regs.geom.prim_config.mode);

    update_gl_state(gpu);

    u32 nverts = gpu->regs.geom.nverts;

    // gsh must be emulated in sw
    if (ctremu.hwvshaders && !gpu->regs.geom.config.use_gsh) {
        setup_vbos_hw(gpu, gpu->regs.geom.vtx_off, gpu->regs.geom.nverts);
    } else {
        fvec4 vshout[nverts][16];
        run_vsh(gpu, gpu->regs.geom.vtx_off, nverts, vshout);
        if (gpu->regs.geom.config.use_gsh) {
            ShaderUnit gsh;
            init_gsh(gpu, &gsh);

            // there are 3 geom shader modes, rn we only care about the
            // normal mode

            if (gpu->regs.geom.gsh_misc0.mode != 0)
                lwarn("unknown geoshader mode");

            int vshoutct = gpu->regs.geom.vsh_outmap_total1 + 1;
            int gshinct = gpu->regs.gsh.inconfig.inattrs + 1;
            int stride = gshinct / vshoutct;

            for (int p = 0; p < nverts; p += stride) {
                for (int v = 0; v < stride; v++) {
                    memcpy(gsh.v + v * vshoutct, vshout[p + v],
                           vshoutct * sizeof(fvec4));
                }
                pica_shader_exec(&gsh);
            }

            draw_gsh_out(gpu, &gsh);
            return;
        } else {
            Vertex vbuf[nverts];
            for (int i = 0; i < nverts; i++) {
                store_vtx(gpu, i, vbuf, vshout[i]);
            }
            glBufferData(GL_ARRAY_BUFFER, sizeof vbuf, vbuf, GL_STREAM_DRAW);
        }
    }

    glDrawArrays(prim_mode[gpu->regs.geom.prim_config.mode], 0,
                 gpu->regs.geom.nverts);
}

static const GLuint indextypes[2] = {GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT};

void gpu_drawelements(GPU* gpu) {
    linfo("drawing elements nverts=%d primmode=%d", gpu->regs.geom.nverts,
          gpu->regs.geom.prim_config.mode);

    update_gl_state(gpu);

    u32 minind = 0xffff, maxind = 0;
    void* indexbuf =
        PTR(gpu->regs.geom.attr_base * 8 + gpu->regs.geom.indexbufoff);
    for (int i = 0; i < gpu->regs.geom.nverts; i++) {
        int idx;
        if (gpu->regs.geom.indexfmt) {
            idx = ((u16*) indexbuf)[i];
        } else {
            idx = ((u8*) indexbuf)[i];
        }
        if (idx < minind) minind = idx;
        if (idx > maxind) maxind = idx;
    }
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 gpu->regs.geom.nverts * BIT(gpu->regs.geom.indexfmt), indexbuf,
                 GL_STREAM_DRAW);

    u32 nverts = gpu->regs.geom.nverts;
    u32 nbufverts = maxind + 1 - minind;

    if (ctremu.hwvshaders && !gpu->regs.geom.config.use_gsh) {
        setup_vbos_hw(gpu, minind, nbufverts);
    } else {
        fvec4 vshout[nbufverts][16];
        run_vsh(gpu, minind, nbufverts, vshout);
        if (gpu->regs.geom.config.use_gsh) {
            ShaderUnit gsh;
            init_gsh(gpu, &gsh);

            if (gpu->regs.geom.gsh_misc0.mode != 0)
                lwarn("unknown geoshader mode");

            int vshoutct = gpu->regs.geom.vsh_outmap_total1 + 1;
            int gshinct = gpu->regs.gsh.inconfig.inattrs + 1;
            int stride = gshinct / vshoutct;

            for (int p = 0; p < nverts; p += stride) {
                for (int v = 0; v < stride; v++) {
                    int idx;
                    if (gpu->regs.geom.indexfmt) {
                        idx = ((u16*) indexbuf)[p + v];
                    } else {
                        idx = ((u8*) indexbuf)[p + v];
                    }
                    memcpy(gsh.v + v * vshoutct, vshout[idx],
                           vshoutct * sizeof(fvec4));
                }
                pica_shader_exec(&gsh);
            }

            draw_gsh_out(gpu, &gsh);
            return;
        } else {
            Vertex vbuf[nbufverts];
            for (int i = 0; i < nbufverts; i++) {
                store_vtx(gpu, i, vbuf, vshout[i]);
            }
            glBufferData(GL_ARRAY_BUFFER, sizeof vbuf, vbuf, GL_STREAM_DRAW);
        }
    }

    glDrawElementsBaseVertex(prim_mode[gpu->regs.geom.prim_config.mode], nverts,
                             indextypes[gpu->regs.geom.indexfmt], 0, -minind);
}

void gpu_drawimmediate(GPU* gpu) {
    u32 nattrs = gpu->regs.geom.vsh_num_attr + 1;
    u32 nverts = gpu->immattrs.size / nattrs;

    linfo("drawing immediate mode nverts=%d primmode=%d", nverts,
          gpu->regs.geom.prim_config.mode);

    update_gl_state(gpu);

    if (ctremu.hwvshaders && !gpu->regs.geom.config.use_gsh) {
        setup_fixattrs_hw(gpu);
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
    } else {
        AttrConfig cfg;
        vtx_loader_imm_setup(gpu, cfg);
        fvec4 vshout[nverts][16];
        dispatch_vsh(gpu, cfg, 0, nverts, vshout);
        if (gpu->regs.geom.config.use_gsh) {
            ShaderUnit gsh;
            init_gsh(gpu, &gsh);

            if (gpu->regs.geom.gsh_misc0.mode != 0)
                lwarn("unknown geoshader mode");

            int vshoutct = gpu->regs.geom.vsh_outmap_total1 + 1;
            int gshinct = gpu->regs.gsh.inconfig.inattrs + 1;
            int stride = gshinct / vshoutct;

            for (int p = 0; p < nverts; p += stride) {
                for (int v = 0; v < stride; v++) {
                    memcpy(gsh.v + v * vshoutct, vshout[p + v],
                           vshoutct * sizeof(fvec4));
                }
                pica_shader_exec(&gsh);
            }

            draw_gsh_out(gpu, &gsh);
            return;
        } else {
            Vertex vbuf[nverts];
            for (int i = 0; i < nverts; i++) {
                store_vtx(gpu, i, vbuf, vshout[i]);
            }
            glBufferData(GL_ARRAY_BUFFER, sizeof vbuf, vbuf, GL_STREAM_DRAW);
        }
    }

    glDrawArrays(prim_mode[gpu->regs.geom.prim_config.mode], 0, nverts);

    Vec_free(gpu->immattrs);
}