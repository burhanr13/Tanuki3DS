#include "cam.h"

#include "3ds.h"

DECL_PORT(cam) {
    u32* cmdbuf = PTR(cmd_addr);
    auto cam = &s->services.cam;
    switch (cmd.command) {
        case 0x0001:
            linfo("StartCapture");
            cam->capturing = true;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0002:
            linfo("StopCapture");
            cam->capturing = false;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0003:
            linfo("IsBusy");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0;
            break;
        case 0x0004:
            linfo("ClearBuffer");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0005:
            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0;
            cmdbuf[3] = srvobj_make_handle(s, &cam->vsyncEvent.hdr);
            linfo("GetVSyncInterruptEvent handle %08x", cmdbuf[3]);
            break;
        case 0x0006:
            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0;
            cmdbuf[3] = srvobj_make_handle(s, &cam->errEvent.hdr);
            linfo("GetBufferErrorInterruptEvent handle %08x", cmdbuf[3]);
            break;
        case 0x0007: {
            u32 dst = cmdbuf[1];
            u32 port = cmdbuf[2];
            u32 size = cmdbuf[3];
            s16 unit = cmdbuf[4];
            // port bits 0,1 for left,right cameras for 3d camera
            // we only render left eye screen so ignore commands for right
            // camera
            if (port & 1) cam->dstAddr = dst;
            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0;
            cmdbuf[3] = srvobj_make_handle(s, &cam->recvEvent.hdr);
            linfo("SetReceiving size %d unit %d handle %08x", size, unit,
                  cmdbuf[3]);
            if (cam->trimming) {
                linfo("trimming params: %d %d %d %d", cam->x0, cam->y0, cam->x1,
                      cam->y1);
                cam->width = cam->x1 - cam->x0;
                cam->height = cam->y1 - cam->y0;
            }
            break;
        }
        case 0x0009: {
            s16 lines = cmdbuf[2];
            s16 w = cmdbuf[3];
            s16 h = cmdbuf[4];
            linfo("SetTransferLines %d %d %d", lines, w, h);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x000a: {
            s16 w = cmdbuf[1];
            s16 h = cmdbuf[2];
            linfo("GetMaxLines %d %d", w, h);
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = h; // what is a line in this case
            break;
        }
        case 0x000b: {
            u32 bytes = cmdbuf[2];
            s16 w = cmdbuf[3];
            s16 h = cmdbuf[4];
            linfo("SetTransferBytes %d %d %d", bytes, w, h);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x000c: {
            linfo("GetTransferBytes");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = cam->width * cam->height * 2;
            break;
        }
        case 0x000d: {
            s16 w = cmdbuf[1];
            s16 h = cmdbuf[2];
            linfo("GetMaxBytes %d %d", w, h);
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = 3 * w * h;
            break;
        }
        case 0x000e:
            linfo("SetTrimming");
            cam->trimming = cmdbuf[2];
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0010:
            linfo("SetTrimmingParams");
            cam->x0 = cmdbuf[2];
            cam->y0 = cmdbuf[3];
            cam->x1 = cmdbuf[4];
            cam->y1 = cmdbuf[5];
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0013:
            linfo("Activate");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0019:
            linfo("SetAutoExposure");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x001b:
            linfo("SetAutoWhiteBalance");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x001f: {
            u32 size = cmdbuf[2];
            linfo("SetSize %d", size);
            static const int sizes[8][2] = {
                {640, 480}, {320, 240}, {160, 120}, {352, 288},
                {176, 144}, {256, 192}, {512, 384}, {400, 240},
            };
            cam->width = sizes[size & 7][0];
            cam->height = sizes[size & 7][1];
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0025: {
            u32 fmt = cmdbuf[2];
            linfo("SetOutputFormat %d", fmt);
            cam->rgb = fmt;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0028:
            linfo("SetNoiseFilter");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0029:
            linfo("SynchronizeVSyncTiming");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0038:
            linfo("PlayShutterSound");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0039:
            linfo("DriverInitialize");
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

void cam_send_data(E3DS* s, void* src) {
    auto cam = &s->services.cam;
    if (!cam->dstAddr) return;
    void* dst = PTR(cam->dstAddr);
    u32 w = cam->width;
    u32 h = cam->height;
    linfo("sending camera image %dx%d size=%d", w, h, w * h * 2);
    if (src) memcpy(dst, src, w * h * 2);
    cam->dstAddr = 0;
    linfo("signaling camera event");
    event_signal(s, &cam->recvEvent);
}
