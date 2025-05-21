#include "gpu.h"

#include "3ds.h"
#include "emulator.h"
#include "kernel/memory.h"

#include "renderer_gl.h"
#include "shader.h"
#include "shaderdec.h"
#include "shadergen_fs.h"

#include "gpuptr.inc"

void gpu_init(GPU* gpu) {
    LRU_init(gpu->fbs);
    // ensure this is pointing to something
    gpu->curfb = &gpu->fbs.root;
    LRU_init(gpu->textures);
    LRU_init(gpu->vshaders_sw);
    LRU_init(gpu->vshaders_hw);
    LRU_init(gpu->fshaders);

    gpu_vshrunner_init(gpu);

    renderer_gl_init(&gpu->gl, gpu);
}

void gpu_destroy(GPU* gpu) {
    renderer_gl_destroy(&gpu->gl, gpu);

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
            gpu_draw(gpu, false, false);
            break;
        case GPUREG(geom.drawelements):
            gpu_draw(gpu, true, false);
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
                gpu_draw(gpu, false, true);
            }
            break;
        case GPUREG(gsh.floatuniform_data[0])... GPUREG(
            gsh.floatuniform_data[7]): {
            u32 idx = gpu->regs.gsh.floatuniform_idx;
            if (idx >= 96) {
                lwarnonce("writing to out of bound uniform");
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
                lwarnonce("writing to out of bound uniform");
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
FBInfo* gpu_fbcache_find_within(GPU* gpu, u32 color_paddr) {
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

TexInfo* gpu_texcache_find_within(GPU* gpu, u32 paddr) {
    TexInfo* tex = nullptr;
    for (int i = 0; i < TEX_MAX; i++) {
        if (gpu->textures.d[i].paddr <= paddr &&
            paddr < gpu->textures.d[i].paddr + gpu->textures.d[i].size) {
            tex = &gpu->textures.d[i];
            break;
        }
    }
    if (tex) LRU_use(gpu->textures, tex);
    return tex;
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

void gpu_display_transfer(GPU* gpu, u32 paddr, int yoff, bool scalex,
                          bool scaley, bool vflip, int screenid) {
    gpu_gl_display_transfer(gpu, paddr, yoff, scalex, scaley, vflip, screenid);
}

void gpu_render_lcd_fb(GPU* gpu, u32 paddr, u32 fmt, int screenid) {
    gpu_gl_render_lcd_fb(gpu, paddr, fmt, screenid);
}

void gpu_texture_copy(GPU* gpu, u32 srcpaddr, u32 dstpaddr, u32 size,
                      u32 srcpitch, u32 srcgap, u32 dstpitch, u32 dstgap) {
    gpu_gl_texture_copy(gpu, srcpaddr, dstpaddr, size, srcpitch, srcgap,
                        dstpitch, dstgap);
}

void gpu_clear_fb(GPU* gpu, u32 paddr, u32 len, u32 value, u32 datasz) {
    gpu_gl_clear_fb(gpu, paddr, len, value, datasz);
}

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

void gpu_write_outmap_vtx(GPU* gpu, Vertex* dst, fvec4* src) {
    // if a semantic is specified multiple times in the outmap we only pick the
    // first one
    u32 written = 0;
    for (int o = 0; o < 7; o++) {
        for (int j = 0; j < 4; j++) {
            u8 sem = gpu->regs.raster.sh_outmap[o][j];
            if (sem < 0x18 && !(written & BIT(sem)))
                dst->semantics[sem] = src[o][j];
            written |= BIT(sem);
        }
    }
}

void gpu_init_vsh(GPU* gpu, ShaderUnit* shu) {
    shu->code = (PICAInstr*) gpu->vsh.progdata;
    shu->opdescs = (OpDesc*) gpu->vsh.opdescs;
    shu->entrypoint = gpu->regs.vsh.entrypoint;
    shu->outmap_mask = gpu->regs.vsh.outmap_mask;
    shu->c = gpu->vsh.floatuniform;
    shu->i = gpu->regs.vsh.intuniform;
    shu->b = gpu->regs.vsh.booluniform;
}

void gpu_init_gsh(GPU* gpu, ShaderUnit* shu) {
    shu->code = (PICAInstr*) gpu->gsh.progdata;
    shu->opdescs = (OpDesc*) gpu->gsh.opdescs;
    shu->entrypoint = gpu->regs.gsh.entrypoint;
    shu->outmap_mask = gpu->regs.gsh.outmap_mask;
    shu->c = gpu->gsh.floatuniform;
    shu->i = gpu->regs.gsh.intuniform;
    shu->b = gpu->regs.gsh.booluniform;
    Vec_init(shu->gsh.outvtx);
}

void vsh_run_range(GPU* gpu, AttrConfig cfg, int srcoff, int dstoff, int count,
                   fvec4 (*vbuf)[16]) {
    ShaderUnit vsh;
    gpu_init_vsh(gpu, &vsh);
    for (int i = 0; i < count; i++) {
        load_vtx(gpu, cfg, srcoff + i, vsh.v);
        gpu->vsh_runner.shaderfunc(&vsh);
        shader_write_outmap(&vsh, vbuf[dstoff + i]);
    }
}

void vsh_thrd_func(GPU* gpu) {
    int id = gpu->vsh_runner.cur++;

    while (true) {
        while (!gpu->vsh_runner.ready[id]) sched_yield();
        gpu->vsh_runner.ready[id] = false;

        if (gpu->vsh_runner.die) return;

        vsh_run_range(gpu, gpu->vsh_runner.attrcfg,
                      gpu->vsh_runner.base + id * gpu->vsh_runner.count,
                      id * gpu->vsh_runner.count, gpu->vsh_runner.count,
                      gpu->vsh_runner.vbuf);

        gpu->vsh_runner.cur++;
    }
}

void gpu_vshrunner_init(GPU* gpu) {
    gpu->vsh_runner.cur = 0;
    for (int i = 0; i < ctremu.vshthreads; i++) {
        gpu->vsh_runner.ready[i] = false;
        pthread_create(&gpu->vsh_runner.threads[i], nullptr,
                       (void*) vsh_thrd_func, gpu);
    }
    while (gpu->vsh_runner.cur < ctremu.vshthreads);
}

void gpu_vshrunner_destroy(GPU* gpu) {
    gpu->vsh_runner.die = true;
    for (int i = 0; i < ctremu.vshthreads; i++) {
        gpu->vsh_runner.ready[i] = true;
    }
    for (int i = 0; i < ctremu.vshthreads; i++) {
        pthread_join(gpu->vsh_runner.threads[i], nullptr);
    }
}

void dispatch_vsh(GPU* gpu, void* attrcfg, int base, int count, void* vbuf) {
    if (ctremu.shaderjit) {
        if (gpu->vsh.code_dirty) {
            ShaderUnit shu;
            gpu_init_vsh(gpu, &shu);
            gpu->vsh_runner.shaderfunc = shaderjit_get(gpu, &shu);
            gpu->vsh.code_dirty = false;
        }
    } else {
        gpu->vsh_runner.shaderfunc = pica_shader_exec;
    }

    gpu->vsh_runner.attrcfg = attrcfg;
    gpu->vsh_runner.vbuf = vbuf;
    gpu->vsh_runner.base = base;
    gpu->vsh_runner.count = count / (ctremu.vshthreads + 1);
    if (gpu->vsh_runner.count) {
        gpu->vsh_runner.cur = 0;
        for (int i = 0; i < ctremu.vshthreads; i++) {
            gpu->vsh_runner.ready[i] = true;
        }
        vsh_run_range(
            gpu, attrcfg, base + gpu->vsh_runner.count * ctremu.vshthreads,
            gpu->vsh_runner.count * ctremu.vshthreads,
            gpu->vsh_runner.count + count % (ctremu.vshthreads + 1), vbuf);
        while (gpu->vsh_runner.cur < ctremu.vshthreads);
    } else {
        vsh_run_range(gpu, attrcfg, base, 0, count, vbuf);
    }
}

void gpu_run_vsh(GPU* gpu, bool immediate, int basevert, int nbufverts,
                 fvec4 (*vshout)[16]) {
    AttrConfig cfg;
    if (immediate) {
        vtx_loader_imm_setup(gpu, cfg);
    } else {
        vtx_loader_setup(gpu, cfg);
    }

    // run the vertex shader (possibly parallelized)
    dispatch_vsh(gpu, cfg, basevert, nbufverts, vshout);
}

void gpu_run_gsh(GPU* gpu, ShaderUnit* gsh, bool elements, int basevert,
                 int nverts, fvec4 (*vshout)[16], bool indexsize,
                 void* indexbuf) {
    // there are 3 geom shader modes, rn we only care about the
    // normal mode

    if (gpu->regs.geom.gsh_misc0.mode != 0) {
        lwarn("unknown geoshader mode");
        return;
    }

    int vshoutct = gpu->regs.geom.vsh_outmap_total1 + 1;
    int gshinct = gpu->regs.gsh.inconfig.inattrs + 1;
    int stride = gshinct / vshoutct;

    for (int p = 0; p < nverts; p += stride) {
        for (int v = 0; v < stride; v++) {
            int idx = p + v;
            if (elements) {
                if (indexsize) {
                    idx = ((u16*) indexbuf)[idx] - basevert;
                } else {
                    idx = ((u8*) indexbuf)[idx] - basevert;
                }
            }
            for (int i = 0; i < vshoutct; i++) {
                int attr = v * vshoutct + i;
                attr = (gpu->regs.gsh.permutation >> 4 * attr) & 0xf;
                memcpy(gsh->v[attr], vshout[idx][i], sizeof(fvec4));
            }
        }

        pica_shader_exec(gsh);
    }
}

void gpu_draw(GPU* gpu, bool elements, bool immediate) {
    gpu_gl_draw(gpu, elements, immediate);
}
