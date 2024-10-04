#include "gpu.h"

#include "renderer_gl.h"

#include "shader.h"

#define PTR(addr) ((void*) &gpu->mem[addr])

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
    u32 mantissa = i & 0x7fffff;
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

void gpu_clear_fb(GPU* gpu, u32 color) {
    glClearColor((color >> 24) / 256.f, ((color >> 16) & 0xff) / 256.f,
                 ((color >> 8) & 0xff) / 256.f, (color & 0xff) / 256.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void gpu_write_internalreg(GPU* gpu, u16 id, u32 param, u32 mask) {
    linfo("command %03x (0x%08x) & %08x (%f)", id, param, mask, I2F(param));
    switch (id) {
        case GPUREG(geom.drawarrays):
            gpu_drawarrays(gpu);
            break;
        case GPUREG(geom.drawelements):
            gpu_drawelements(gpu);
            break;
        case GPUREG(geom.fixattr_data[0])... GPUREG(geom.fixattr_data[2]): {
            fvec* fattr;
            if (gpu->io.geom.fixattr_idx == 0xf) {
                lwarn("immediate mode");
                break;
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
                    break;
                }
            }
            break;
        }
        case GPUREG(VSH.floatuniform_data[0])... GPUREG(
            VSH.floatuniform_data[7]): {
            fvec* uniform = &gpu->cst[gpu->io.VSH.floatuniform_idx];
            if (gpu->io.VSH.floatuniform_mode) {
                (*uniform)[3 - gpu->curunifi] = I2F(param);
                if (++gpu->curunifi == 4) {
                    gpu->curunifi = 0;
                    gpu->io.VSH.floatuniform_idx++;
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
                        gpu->io.VSH.floatuniform_idx++;
                        break;
                    }
                }
            }
            break;
        }
        case GPUREG(VSH.codetrans_data[0])... GPUREG(VSH.codetrans_data[8]):
            gpu->progdata[gpu->io.VSH.codetrans_idx++] = param;
            break;
        case GPUREG(VSH.opdescs_data[0])... GPUREG(VSH.opdescs_data[8]):
            gpu->opdescs[gpu->io.VSH.opdescs_idx++] = param;
            break;
        default:
            gpu->io.w[id] &= ~mask;
            gpu->io.w[id] |= param & mask;
            break;
    }
}

void gpu_run_command_list(GPU* gpu, u32* addr, u32 size) {
    u32* cur = addr;
    u32* end = addr + size;
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

void load_vtx(GPU* gpu, int i) {
    memcpy(gpu->in, gpu->fixattrs, sizeof gpu->in);
    for (int vbo = 0; vbo < 12; vbo++) {
        void* vtx =
            PTR(gpu->io.geom.attr_base * 8 + gpu->io.geom.attrbuf[vbo].offset +
                i * gpu->io.geom.attrbuf[vbo].size);
        for (int c = 0; c < gpu->io.geom.attrbuf[vbo].count; c++) {
            int attr = (gpu->io.geom.attrbuf[vbo].comp >> 4 * c) & 0xf;
            if (attr >= 0xc) {
                vtx += 4 * (attr - 0xb);
                continue;
            }
            attr = (gpu->io.VSH.permutation >> 4 * attr) & 0xf;
            int type = (gpu->io.geom.attr_format >> 4 * attr) & 0xf;
            int size = (type >> 2) + 1;
            type &= 3;
            switch (type) {
                case ATTR_S8: {
                    s8* ptr = vtx;
                    for (int k = 0; k < size; k++) {
                        gpu->in[attr][k] = *ptr++;
                    }
                    vtx = ptr;
                    break;
                }
                case ATTR_U8: {
                    u8* ptr = vtx;
                    for (int k = 0; k < size; k++) {
                        gpu->in[attr][k] = *ptr++;
                    }
                    vtx = ptr;
                    break;
                }
                case ATTR_S16: {
                    s16* ptr = vtx;
                    for (int k = 0; k < size; k++) {
                        gpu->in[attr][k] = *ptr++;
                    }
                    vtx = ptr;
                    break;
                }
                case ATTR_F32: {
                    float* ptr = vtx;
                    for (int k = 0; k < size; k++) {
                        gpu->in[attr][k] = *ptr++;
                    }
                    vtx = ptr;
                    break;
                }
            }
            if (size < 2) gpu->in[attr][1] = 0.0f;
            if (size < 3) gpu->in[attr][2] = 0.0f;
            if (size < 4) gpu->in[attr][3] = 1.0f;
        }
    }
}

void store_vtx(GPU* gpu, int i, Vertex* vbuf) {
    for (int o = 0; o < 7; o++) {
        for (int j = 0; j < 4; j++) {
            u8 sem = gpu->io.raster.sh_outmap[o][j];
            if (sem < 0x18) vbuf[i].semantics[sem] = gpu->out[o][j];
        }
    }
}

static int prim_mode[4] = {GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN,
                           GL_TRIANGLES};

void gpu_drawarrays(GPU* gpu) {
    linfo("drawing arrays nverts=%d", gpu->io.geom.nverts);
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
    int minind = 0xffff, maxind = 0;
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
