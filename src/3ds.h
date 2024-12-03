#ifndef _3DS_H
#define _3DS_H

#include "arm/arm_core.h"
#include "common.h"
#include "kernel.h"
#include "loader.h"
#include "memory.h"
#include "pica/gpu.h"
#include "process.h"
#include "scheduler.h"
#include "services.h"
#include "srv.h"
#include "thread.h"

#define CPU_CLK 268000000
#define FPS 60

#define SCREEN_WIDTH 400
#define SCREEN_WIDTH_BOT 320
#define SCREEN_HEIGHT 240

typedef struct _3DS {
    ArmCore cpu;

    GPU gpu;

    int fcram_fd;
    int vram_fd;

    u8* physmem;
    u8* virtmem;

    KProcess process;

    ServiceData services;

    RomImage romimage;

    bool frame_complete;

    Scheduler sched;
} HLE3DS;

#define R(n) s->cpu.r[n]
#define PTR(addr) ((void*) &s->virtmem[addr])

#define PAGE_SIZE BIT(12)

#define FCRAMSIZE BIT(27)
#define VRAMSIZE (6 * BIT(20))

#define FCRAM_PBASE BIT(29)
#define VRAM_PBASE 0x18000000

#define HEAP_BASE BIT(27)

#define STACK_BASE BIT(28)
#define STACK_SIZE BIT(14)

#define LINEAR_HEAP_BASE 0x14000000

#define VRAMBASE 0x1f000000

#define DSPMEM 0x1ff50000
#define DSPMEMSIZE BIT(15)
#define DSPBUFBIT BIT(17)

#define CONFIG_MEM 0x1ff80000
#define SHARED_PAGE 0x1ff81000

#define TLS_BASE 0x1ff82000
#define TLS_SIZE 0x200
#define IPC_CMD_OFF 0x80

void hle3ds_init(HLE3DS* s, char* romfile);
void hle3ds_destroy(HLE3DS* s);

void hle3ds_update_datetime(HLE3DS* s);

void hle3ds_run_frame(HLE3DS* s);

#endif
