#ifndef GSP_H
#define GSP_H

#include "kernel/memory.h"
#include "kernel/thread.h"
#include "scheduler.h"

#include "srv.h"

enum {
    GSPEVENT_PSC0,
    GSPEVENT_PSC1,
    GSPEVENT_VBLANK0,
    GSPEVENT_VBLANK1,
    GSPEVENT_PPF,
    GSPEVENT_P3D,
    GSPEVENT_DMA,
};

typedef struct {
    u8 cur;
    u8 count;
    u8 err;
    u8 flags;
    u8 errvblank0[4];
    u8 errvblank1[4];
    u8 queue[0x34];
} GSPInterruptQueue;

typedef struct {
    u8 idx;
    struct {
        u8 newdataflag : 1;
        u8 : 7;
    };
    u16 _pad;
    struct {
        u32 active;
        u32 left_vaddr;
        u32 right_vaddr;
        u32 stride;
        u32 format;
        u32 status;
        u32 unk;
    } fbs[2];
    u32 _pad2;
} GSPFBInfo;

typedef struct {
    u8 cur;
    u8 count;
    u8 flags;
    u8 flags2;
    u8 err;
    u8 pad[27];
    struct {
        u8 id;
        u8 unk;
        u8 unk2;
        u8 mode;
        u32 args[7];
    } d[15];
} GSPCommandQueue;

typedef struct {
    GSPInterruptQueue interrupts[8];
    GSPFBInfo fbinfo[12][2];
    GSPCommandQueue commands[4];
} GSPSharedMem;

typedef struct {
    u32 vaddr;
    u32 fmt;
    bool wasDisplayTransferred;
} LCDFBInfo;

typedef struct {
    KEvent* event;
    KSharedMem sharedmem;
    bool registered;

    FIFO(LCDFBInfo, 2) lcdfbs[2];
} GSPData;

DECL_PORT(gsp_gpu);

void gsp_handle_event(E3DS* s, SchedEventArg arg);
void gsp_handle_command(E3DS* s);

#endif
