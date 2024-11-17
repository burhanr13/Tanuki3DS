#ifndef HID_H
#define HID_H

#include "../memory.h"
#include "../srv.h"
#include "../svc_types.h"
#include "../thread.h"

enum {
    HIDEVENT_PAD0,
    HIDEVENT_PAD1,
    HIDEVENT_ACCEL,
    HIDEVENT_GYRO,
    HIDEVENT_DBGPAD,
    HIDEVENT_MAX
};

typedef union {
    u32 w;
    struct {
        u32 a : 1;
        u32 b : 1;
        u32 select : 1;
        u32 start : 1;
        u32 right : 1;
        u32 left : 1;
        u32 up : 1;
        u32 down : 1;
        u32 r : 1;
        u32 l : 1;
        u32 x : 1;
        u32 y : 1;
        u32 gpio : 2;
        u32 unused : 14;
        u32 cright : 1;
        u32 cleft : 1;
        u32 cup : 1;
        u32 cdown : 1;
    };
} PadState;

typedef struct {
    struct {
        u64 time;
        u64 prevtime;
        u64 idx;
        float slider3d;
        u32 btns;
        s16 cx;
        s16 cy;
        u32 _padding;
        struct {
            u32 held;
            u32 pressed;
            u32 released;
            s16 cx;
            s16 cy;
        } entries[8];
    } pad;
    struct {
        u64 time;
        u64 prevtime;
        u64 idx;
        u64 raw;
        struct {
            u16 x;
            u16 y;
            u32 pressed;
        } entries[8];
    } touch;
} HIDSharedMem;

typedef struct {
    KSharedMem sharedmem;
    KEvent events[HIDEVENT_MAX];
} HIDData;

DECL_PORT(hid);

void hid_update_pad(HLE3DS* s, u32 btns, s32 cx, s32 cy);
void hid_update_touch(HLE3DS* s, u16 x, u16 y, bool pressed);

#endif
