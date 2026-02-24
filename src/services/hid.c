#include "hid.h"

#include "3ds.h"

#define HIDMEM ((HIDSharedMem*) PPTR(s->services.hid.sharedmem.paddr))

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
            *(float*) &cmdbuf[2] = HID_GYRO_DPS_COEFF;
            break;
        case 0x0016:
            linfo("GetGyroscopeLowCalibrateParam");
            cmdbuf[0] = IPCHDR(6, 0);
            cmdbuf[1] = 0;
            s16* params = (s16*) &cmdbuf[2];
            // values from Panda3DS
            params[0] = 0;
            params[1] = 6700;
            params[2] = -6700;
            params[3] = 0;
            params[4] = 6700;
            params[5] = -6700;
            params[6] = 0;
            params[7] = 6700;
            params[8] = -6700;
            break;
        default:
            lwarn("unknown command 0x%04x (%x,%x,%x,%x,%x)", cmd.command,
                  cmdbuf[1], cmdbuf[2], cmdbuf[3], cmdbuf[4], cmdbuf[5]);
            cmdbuf[0] = IPCHDR(1, 0);
            cmdbuf[1] = 0;
            break;
    }
}

void hid_update_pad(E3DS* s, u32 btns, s32 cx, s32 cy) {
    int curidx = (HIDMEM->pad.idx + 1) % 8;
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

    linfo("signaling hid event");
    event_signal(s, &s->services.hid.events[HIDEVENT_PAD0]);
}

void hid_update_touch(E3DS* s, u16 x, u16 y, bool pressed) {
    int curidx = (HIDMEM->touch.idx + 1) % 8;
    HIDMEM->touch.idx = curidx;

    if (curidx == 0) {
        HIDMEM->touch.prevtime = HIDMEM->touch.time;
        HIDMEM->touch.time = s->sched.now;
    }

    HIDMEM->touch.entries[curidx].x = x;
    HIDMEM->touch.entries[curidx].y = y;
    HIDMEM->touch.entries[curidx].pressed = pressed;

    event_signal(s, &s->services.hid.events[HIDEVENT_PAD1]);
}

void hid_update_accel(E3DS* s, s16 x, s16 y, s16 z) {
    int curidx = (HIDMEM->accel.idx + 1) % 8;
    HIDMEM->accel.idx = curidx;

    if (curidx == 0) {
        HIDMEM->accel.prevtime = HIDMEM->accel.time;
        HIDMEM->accel.time = s->sched.now;
    }

    HIDMEM->accel.entries[curidx].x = x;
    HIDMEM->accel.entries[curidx].y = y;
    HIDMEM->accel.entries[curidx].z = z;

    event_signal(s, &s->services.hid.events[HIDEVENT_ACCEL]);
}

void hid_update_gyro(E3DS* s, s16 x, s16 y, s16 z) {
    int curidx = (HIDMEM->gyro.idx + 1) % 32;
    HIDMEM->gyro.idx = curidx;

    if (curidx == 0) {
        HIDMEM->gyro.prevtime = HIDMEM->gyro.time;
        HIDMEM->gyro.time = s->sched.now;
    }

    HIDMEM->gyro.entries[curidx].x = x;
    HIDMEM->gyro.entries[curidx].y = y;
    HIDMEM->gyro.entries[curidx].z = z;

    event_signal(s, &s->services.hid.events[HIDEVENT_GYRO]);
}
