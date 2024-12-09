#include "3ds.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cpu.h"
#include "loader.h"
#include "svc_types.h"

void e3ds_init(E3DS* s, char* romfile) {
    memset(s, 0, sizeof *s);

    s->sched.master = s;

    cpu_init(s);

    gpu_vshrunner_init(&s->gpu);

    e3ds_memory_init(s);

    u32 entrypoint = 0;

    char* ext = strrchr(romfile, '.');
    if (!ext) {
        eprintf("unsupported file format\n");
        exit(1);
    }
    if (!strcmp(ext, ".elf") || !strcmp(ext, ".axf")) {
        entrypoint = load_elf(s, romfile);
    } else if (!strcmp(ext, ".3ds") || !strcmp(ext, ".cci") ||
               !strcmp(ext, ".ncsd")) {
        entrypoint = load_ncsd(s, romfile);
    } else if (!strcmp(ext, ".cxi") || !strcmp(ext, ".app") ||
               !strcmp(ext, ".ncch")) {
        entrypoint = load_ncch(s, romfile, 0);
    } else {
        eprintf("unsupported file format\n");
        exit(1);
    }
    if (entrypoint == -1) {
        eprintf("failed to load rom\n");
        exit(1);
    }

    e3ds_vmmap(s, DSPMEM, DSPMEMSIZE, PERM_RW, MEMST_STATIC, false);
    e3ds_vmmap(s, DSPMEM | DSPBUFBIT, DSPMEMSIZE, PERM_RW, MEMST_STATIC, false);

    e3ds_vmmap(s, CONFIG_MEM, PAGE_SIZE, PERM_R, MEMST_STATIC, false);
    *(u8*) PTR(CONFIG_MEM + 0x14) = 1;
    *(u32*) PTR(CONFIG_MEM + 0x40) = FCRAMSIZE;

    e3ds_vmmap(s, SHARED_PAGE, PAGE_SIZE, PERM_R, MEMST_STATIC, false);
    *(u32*) PTR(SHARED_PAGE + 4) = 1;

    e3ds_vmmap(s, TLS_BASE, TLS_SIZE * THREAD_MAX, PERM_RW, MEMST_PRIVATE,
               false);

    init_services(s);

    thread_init(s, entrypoint);

    s->process.hdr.type = KOT_PROCESS;
    s->process.hdr.refcount = 1;
    s->process.handles[1] = &s->process.hdr;

    add_event(&s->sched, gsp_handle_event, GSPEVENT_VBLANK0, CPU_CLK / FPS);
}

void e3ds_destroy(E3DS* s) {
    cpu_free(s);

    if (s->romimage.fp) fclose(s->romimage.fp);

    e3ds_memory_destroy(s);
}

void e3ds_update_datetime(E3DS* s) {
    struct {
        u64 time;
        u64 systemtick;
        u32 unk[4];
    }* datetime = PTR(SHARED_PAGE + 0x20);

    datetime->time = (time(NULL) + 2208988800) * 1000;
    datetime->systemtick = s->sched.now;
    datetime->unk[0] = 0xffb0ff0;
}

void e3ds_run_frame(E3DS* s) {
    while (!s->frame_complete) {
        if (!s->cpu.wfe) {
            cpu_run(s, FIFO_peek(s->sched.event_queue).time - s->sched.now);
        }
        run_next_event(&s->sched);
        run_to_present(&s->sched);
        while (s->cpu.wfe && !s->frame_complete) {
            run_next_event(&s->sched);
        }
        e3ds_update_datetime(s);
    }
    s->frame_complete = false;
}