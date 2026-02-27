#include "y2r.h"

#include <math.h>

#include "3ds.h"
#include "video/gpu.h"

void y2r_event(E3DS* s) {
    s->services.y2r.busy = false;
    linfo("y2r conversion complete, enable interrupt = %d",
          s->services.y2r.enableInterrupt);
    if (s->services.y2r.enableInterrupt) {
        event_signal(s, &s->services.y2r.transferend);
    }
}

DECL_PORT(y2r) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0001: {
            u32 fmt = cmdbuf[1];
            linfo("SetInputFormat %d", fmt);
            s->services.y2r.inputFmt = fmt;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0003: {
            u32 fmt = cmdbuf[1];
            linfo("SetOutputFormat %d", fmt);
            s->services.y2r.outputFmt = fmt;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0005: {
            u32 rot = cmdbuf[1];
            linfo("SetRotation %d", rot);
            s->services.y2r.rotation = rot;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0007: {
            u32 blockmode = cmdbuf[1];
            linfo("SetBlockAlignment %d", blockmode);
            s->services.y2r.blockMode = blockmode;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x000d: {
            s->services.y2r.enableInterrupt = cmdbuf[1];
            linfo("SetTransferEndInterrupt");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x000f: {
            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = 0;
            cmdbuf[3] = srvobj_make_handle(s, &s->services.y2r.transferend.hdr);
            linfo("GetTransferEndEvent with handle %x", cmdbuf[3]);
            break;
        }
        case 0x0010: {
            u32 buf = cmdbuf[1];
            u32 sz = cmdbuf[2];
            s16 unit = cmdbuf[3];
            s16 stride = cmdbuf[4];
            linfo("SetSendingY %08x %d %d %d", buf, sz, unit, stride);
            s->services.y2r.srcY.addr = buf;
            s->services.y2r.srcY.pitch = unit;
            s->services.y2r.srcY.gap = stride;
            s->services.y2r.srcY.size = sz;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0011: {
            u32 buf = cmdbuf[1];
            u32 sz = cmdbuf[2];
            s16 unit = cmdbuf[3];
            s16 stride = cmdbuf[4];
            linfo("SetSendingU %08x %d %d %d", buf, sz, unit, stride);
            s->services.y2r.srcU.addr = buf;
            s->services.y2r.srcU.pitch = unit;
            s->services.y2r.srcU.gap = stride;
            s->services.y2r.srcU.size = sz;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0012: {
            u32 buf = cmdbuf[1];
            u32 sz = cmdbuf[2];
            s16 unit = cmdbuf[3];
            s16 stride = cmdbuf[4];
            linfo("SetSendingV %08x %d %d %d", buf, sz, unit, stride);
            s->services.y2r.srcV.addr = buf;
            s->services.y2r.srcV.pitch = unit;
            s->services.y2r.srcV.gap = stride;
            s->services.y2r.srcV.size = sz;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0018: {
            u32 buf = cmdbuf[1];
            u32 sz = cmdbuf[2];
            s16 unit = cmdbuf[3];
            s16 stride = cmdbuf[4];
            linfo("SetReceiving %08x %d %d %d", buf, sz, unit, stride);
            s->services.y2r.dst.addr = buf;
            s->services.y2r.dst.pitch = unit;
            s->services.y2r.dst.gap = stride;
            s->services.y2r.dst.size = sz;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x001a: {
            s16 width = cmdbuf[1];
            linfo("SetInputLineWidth %d", width);
            s->services.y2r.width = width;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x001c: {
            s16 height = cmdbuf[1];
            linfo("SetInputLines %d", height);
            s->services.y2r.height = height;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x001e: {
            // stubbed to just return standard coefficients for now
            linfo("SetCoefficientParams");
            s->services.y2r.coeffs.y_a = 298.082f / 256;
            s->services.y2r.coeffs.r_v = 408.583f / 256;
            s->services.y2r.coeffs.r_off = -222.921f / 256;
            s->services.y2r.coeffs.g_u = -100.291f / 256;
            s->services.y2r.coeffs.g_v = -208.120f / 256;
            s->services.y2r.coeffs.g_off = 135.576f / 256;
            s->services.y2r.coeffs.b_u = 516.412f / 256;
            s->services.y2r.coeffs.b_off = -276.836f / 256;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0020: {
            u32 coef = cmdbuf[1];
            linfo("SetStandardCoefficient %d", coef);
            switch (coef) {
                case 2: // BT.601, coeffs from wikipedia
                    s->services.y2r.coeffs.y_a = 298.082f / 256;
                    s->services.y2r.coeffs.r_v = 408.583f / 256;
                    s->services.y2r.coeffs.r_off = -222.921f / 256;
                    s->services.y2r.coeffs.g_u = -100.291f / 256;
                    s->services.y2r.coeffs.g_v = -208.120f / 256;
                    s->services.y2r.coeffs.g_off = 135.576f / 256;
                    s->services.y2r.coeffs.b_u = 516.412f / 256;
                    s->services.y2r.coeffs.b_off = -276.836f / 256;
                    break;
                default:
                    lwarnonce("unknown coefficient %d", coef);
            }
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0021: {
            u32 coef = cmdbuf[1];
            linfo("GetStandardCoefficient %d", coef);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0022: {
            s16 alpha = cmdbuf[1];
            linfo("SetAlpha %d", alpha);
            s->services.y2r.alpha = alpha;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0026:
            linfo("StartConversion");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            y2r_do_conversion(s);
            s->services.y2r.busy = true;
            // things break if this is instant
            add_event(&s->sched, (SchedulerCallback) y2r_event, 0, 1'500'000);
            break;
        case 0x0027:
            linfo("StopConversion");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            remove_event(&s->sched, (SchedulerCallback) y2r_event, 0);
            s->services.y2r.busy = false;
            break;
        case 0x0028:
            linfo("IsBusyConversion");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = s->services.y2r.busy;
            break;
        case 0x002a:
            linfo("PingProcess");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = 1; // connected
            break;
        case 0x002b:
            linfo("DriverInitialize");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x002c:
            linfo("DriverFinalize");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        default:
            lwarn("unknown command 0x%04x (%x,%x,%x,%x,%x)", cmd.command,
                  cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
    }
}

void y2r_do_conversion(E3DS* s) {
    auto y2r = &s->services.y2r;

    if (y2r->srcY.addr == 0 || y2r->srcU.addr == 0 || y2r->srcV.addr == 0 ||
        y2r->dst.addr == 0) {
        lwarn("null y2r buffers");
        return;
    }

    if (y2r->rotation != 0) lwarnonce("unknown rotation %d", y2r->rotation);

    static const int dstfmtsize[] = {4, 3, 2, 2};

    int ywidth = y2r->srcY.pitch + y2r->srcY.gap;
    int uwidth = y2r->srcU.pitch + y2r->srcU.gap;
    int vwidth = y2r->srcV.pitch + y2r->srcV.gap;
    // these numbers seem to be for a row of tiles regardless of linear/block
    // mode
    int dstwidth =
        (y2r->dst.pitch + y2r->dst.gap) / 8 / dstfmtsize[y2r->outputFmt & 3];

    u8* ydata = PTR(y2r->srcY.addr);
    u8* udata = PTR(y2r->srcU.addr);
    u8* vdata = PTR(y2r->srcV.addr);
    void* out = PTR(y2r->dst.addr);

    float cy = 0, u = 0, v = 0;

    for (int y = 0; y < y2r->height; y++) {
        for (int x = 0; x < y2r->width; x++) {
            switch (y2r->inputFmt) {
                case 1: // yuv420 -> each 2x2 gets one u/v sample
                    cy = ydata[y * ywidth + x] * (1 / 255.f);
                    u = udata[(y / 2) * uwidth + (x / 2)] * (1 / 255.f);
                    v = vdata[(y / 2) * vwidth + (x / 2)] * (1 / 255.f);
                    break;
                default:
                    lwarnonce("unknown input format %d", y2r->inputFmt);
                    return;
            }

            cy *= y2r->coeffs.y_a;

            float r = cy + v * y2r->coeffs.r_v + y2r->coeffs.r_off;
            float g = cy + u * y2r->coeffs.g_u + v * y2r->coeffs.g_v +
                      y2r->coeffs.g_off;
            float b = cy + u * y2r->coeffs.b_u + y2r->coeffs.b_off;
            r = fminf(fmaxf(r, 0), 1);
            g = fminf(fmaxf(g, 0), 1);
            b = fminf(fmaxf(b, 0), 1);

            int outidx = y2r->blockMode ? morton_swizzle(dstwidth, x, y)
                                        : y * dstwidth + x;
            switch (y2r->outputFmt) {
                case 0:
                    ((u32*) out)[outidx] = y2r->alpha | (u32) (b * 255) << 8 |
                                           (u32) (g * 255) << 16 |
                                           (u32) (r * 255) << 24;
                    break;
                case 1:
                    ((u8*) out)[3 * outidx + 0] = b * 255;
                    ((u8*) out)[3 * outidx + 1] = g * 255;
                    ((u8*) out)[3 * outidx + 2] = r * 255;
                    break;
                case 3:
                    ((u16*) out)[outidx] = (u16) (b * 31) |
                                           (u16) (g * 63) << 5 |
                                           (u16) (r * 31) << 11;
                    break;
                default:
                    lwarnonce("unknown output format %d", y2r->outputFmt);
                    return;
            }
        }
    }

    gpu_invalidate_range(&s->gpu, vaddr_to_paddr(y2r->dst.addr),
                         (y2r->dst.pitch / 8 + y2r->dst.gap) * y2r->height);
}
