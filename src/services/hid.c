#include "hid.h"

#include "../3ds.h"

#define HIDMEM ((HIDSharedMem*) PTR(s->services.hid.sharedmem.vaddr))

DECL_PORT(hid) {
    u32* cmdbuf = PTR(cmd_addr);
    switch (cmd.command) {
        case 0x000a:
            cmdbuf[0] = IPCHDR(1, 7);
            cmdbuf[1] = 0;
            cmdbuf[2] = 0x14000000;
            cmdbuf[3] = srvobj_make_handle(s, &s->services.hid.sharedmem.hdr);
            for (int i = 0; i < HIDEVENT_MAX; i++) {
                cmdbuf[4 + i] =
                    srvobj_make_handle(s, &s->services.hid.events[i].hdr);
            }
            linfo("GetIPCHandles with sharedmem %x", cmdbuf[3]);
            break;
        case 0x0011:
            linfo("EnableAccelerometer");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0013:
            linfo("EnableGyroscopeLow");
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
        case 0x0015:
            linfo("GetGyroscopeLowRawToDpsCoefficient");
            cmdbuf[0] = IPCHDR(2, 0);
            cmdbuf[1] = 0;
            *(float*) &cmdbuf[2] = 1.0f;
            break;
        case 0x0016:
            linfo("GetGyroscopeLowCalibrateParam");
            cmdbuf[0] = IPCHDR(6, 0);
            cmdbuf[1] = 0;
            cmdbuf[2] = -1;
            cmdbuf[3] = -1;
            cmdbuf[4] = -1;
            cmdbuf[5] = -1;
            cmdbuf[6] = -1;
            break;
        default:
            lwarn("unknown command 0x%04x (%x,%x,%x,%x,%x)", cmd.command,
                  cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
    }
}

void hid_update_pad(HLE3DS* s, u32 btns, s16 cx, s16 cy) {
    if (!s->services.hid.sharedmem.mapped) return;

    int curidx = 0; //(HIDMEM->pad.idx + 1) % 8;
    HIDMEM->pad.idx = curidx;

    if (curidx == 0) {
        HIDMEM->pad.prevtime = HIDMEM->pad.time;
        HIDMEM->pad.time = s->sched.now;
    }

    u32 prevbtn = HIDMEM->pad.btns;

    cx = (cx * 0x9c) >> 15;
    cy = (cy * 0x9c) >> 15;

    HIDMEM->pad.btns = btns;
    HIDMEM->pad.cx = cx;
    HIDMEM->pad.cy = cy;

    HIDMEM->pad.entries[curidx].cx = cx;
    HIDMEM->pad.entries[curidx].cy = cy;

    HIDMEM->pad.entries[curidx].held = btns;
    HIDMEM->pad.entries[curidx].pressed = btns & ~prevbtn;
    HIDMEM->pad.entries[curidx].released = ~btns & prevbtn;

    linfo("signaling hid event pad0");
    event_signal(s, &s->services.hid.events[HIDEVENT_PAD0]);
}