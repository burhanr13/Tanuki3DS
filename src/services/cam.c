#include "cam.h"

#include "3ds.h"

DECL_PORT(cam) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x0001:
            linfo("StartCapture");
            s->services.cam.capturing = true;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0002:
            linfo("StopCapture");
            s->services.cam.capturing = false;
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
            cmdbuf[3] = srvobj_make_handle(s, &s->services.cam.vsyncEvent.hdr);
            linfo("GetVSyncInterruptEvent handle %08x", cmdbuf[3]);
            break;
        case 0x0006:
            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0;
            cmdbuf[3] = srvobj_make_handle(s, &s->services.cam.errEvent.hdr);
            linfo("GetBufferErrorInterruptEvent handle %08x", cmdbuf[3]);
            break;
        case 0x0007: {
            u32 dst = cmdbuf[1];
            u32 size = cmdbuf[3];
            s->services.cam.dstAddr = dst;
            cmdbuf[0] = IPCHDR(1, 2);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0;
            cmdbuf[3] = srvobj_make_handle(s, &s->services.cam.recvEvent.hdr);
            linfo("SetReceiving size %d handle %08x", size, cmdbuf[3]);
            break;
        }
        case 0x0009: {
            s16 lines = cmdbuf[2];
            s16 w = cmdbuf[3];
            s16 h = cmdbuf[4];
            linfo("SetTransferLines %d %d %d", lines, w, h);
            s->services.cam.width = w;
            s->services.cam.height = h;
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
            s->services.cam.width = w;
            s->services.cam.height = h;
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x000c: {
            linfo("GetTransferBytes");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = s->services.cam.width * s->services.cam.height *
                        (s->services.cam.rgb ? 2 : 3);
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
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        }
        case 0x0025: {
            u32 fmt = cmdbuf[2];
            linfo("SetOutputFormat %d", fmt);
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
    if (!s->services.cam.dstAddr) return;
    void* dst = PTR(s->services.cam.dstAddr);
    u32 w = s->services.cam.width;
    u32 h = s->services.cam.height;
    u32 bpp = s->services.cam.rgb ? 2 : 3;
    if (src) memcpy(dst, src, w * h * bpp);
    s->services.cam.dstAddr = 0;
    linfo("signaling camera event");
    event_signal(s, &s->services.cam.recvEvent);
}
