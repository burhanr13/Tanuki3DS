#ifndef _3DS_H
#define _3DS_H

#include "arm/arm_core.h"
#include "audio/dsp.h"
#include "common.h"
#include "kernel/kernel.h"
#include "kernel/loader.h"
#include "kernel/memory.h"
#include "kernel/process.h"
#include "kernel/thread.h"
#include "scheduler.h"
#include "services/services.h"
#include "services/srv.h"
#include "video/gpu.h"

#define CPU_CLK 268'111'856ull // exact number from citra
#define FPS 60

#define NS_TO_CYCLES(ns) (ns * CPU_CLK / 1'000'000'000)

enum {
    SCREEN_TOP,
    SCREEN_BOT,
};
#define SCREEN_WIDTH_TOP 400
#define SCREEN_WIDTH_BOT 320
#define SCREEN_WIDTH(s) (s == SCREEN_TOP ? SCREEN_WIDTH_TOP : SCREEN_WIDTH_BOT)
#define SCREEN_HEIGHT 240

typedef struct _3DS {
    ArmCore cpu;

    GPU gpu;
    DSP dsp;

    E3DSMemory* mem;

#ifdef FASTMEM
    int mem_fd;
    u8* physmem;
    u8* virtmem;
#endif

    FreeListNode freelist;

    KThread readylist;

    KProcess process;

    ServiceData services;

    RomImage romimage;

    u64 lastAudioFrame;
    bool frame_complete;

    Scheduler sched;
} E3DS;

bool e3ds_init(E3DS* s, char* romfile);
void e3ds_destroy(E3DS* s);

void e3ds_update_datetime(E3DS* s);

void e3ds_run_frame(E3DS* s);

#endif
