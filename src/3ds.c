#include "3ds.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cpu.h"
#include "kernel/loader.h"
#include "kernel/svc_types.h"

bool e3ds_init(E3DS* s, char* romfile) {
    memset(s, 0, sizeof *s);

    s->sched.master = s;

    cpu_init(s);
    gpu_init(&s->gpu);
    memory_init(s);

    services_init(s); // start up and allocate memory for services first

    dsp_lle_init(s); // needs to be after memory,services init

    u32 entrypoint = 0;

    char* ext = strrchr(romfile, '.');
    if (!ext) {
        lerror("unsupported file format");
        e3ds_destroy(s);
        return false;
    }
    if (!strcmp(ext, ".elf") || !strcmp(ext, ".axf")) {
        entrypoint = load_elf(s, romfile);
    } else if (!strcmp(ext, ".3ds") || !strcmp(ext, ".cci") ||
               !strcmp(ext, ".ncsd")) {
        entrypoint = load_ncsd(s, romfile);
    } else if (!strcmp(ext, ".cxi") || !strcmp(ext, ".app") ||
               !strcmp(ext, ".ncch")) {
        entrypoint = load_ncch(s, romfile, 0);
    } else if (!strcmp(ext, ".3dsx")) {
        entrypoint = load_3dsx(s, romfile);
    } else {
        lerror("unsupported file format");
        e3ds_destroy(s);
        return false;
    }
    if (entrypoint == -1) {
        lerror("failed to load rom");
        e3ds_destroy(s);
        return false;
    }

    memory_virtmap(s, VRAM_PBASE, VRAM_VBASE, VRAM_SIZE, PERM_RW, MEMST_STATIC);
    memory_virtmap(s, DSPRAM_PBASE, DSPRAM_VBASE, DSPRAM_SIZE, PERM_RW,
                   MEMST_STATIC);

    // config page and shared page are owned by kernel but whatever
    memory_virtalloc(s, CONFIG_MEM, PAGE_SIZE, PERM_R, MEMST_STATIC);
    *(u8*) PTR(CONFIG_MEM + 0x14) = 1;              // ENVINFO: 1 for prod
    *(u32*) PTR(CONFIG_MEM + 0x40) = FCRAMUSERSIZE; // APPMEMALLOC

    memory_virtalloc(s, SHARED_PAGE, PAGE_SIZE, PERM_R, MEMST_SHARED);
    *(u32*) PTR(SHARED_PAGE + 4) = 1;   // RUNNING_HW: 1 for prod
    *(u8*) PTR(SHARED_PAGE + 0x86) = 1; // "ptm sets this value to 1"

    memory_virtalloc(s, TLS_BASE, TLS_SIZE * THREAD_MAX, PERM_RW,
                     MEMST_PRIVATE);

    thread_init(s, entrypoint);

    s->process.hdr.type = KOT_PROCESS;
    s->process.hdr.refcount = 2; // so closing this handle won't cause problems
    s->process.handles[1] = &s->process.hdr;

    add_event(&s->sched, (SchedulerCallback) gsp_handle_event,
              (void*) GSPEVENT_VBLANK0, CPU_CLK / FPS);

    return true;
}

void e3ds_destroy(E3DS* s) {
    cpu_free(s);

    gpu_destroy(&s->gpu);

    dsp_lle_destroy(s);

    for (int i = 0; i < HANDLE_MAX; i++) {
        if (s->process.handles[i] && !--s->process.handles[i]->refcount)
            kobject_destroy(s, s->process.handles[i]);
    }

    fs_close_all_files(s);

    if (s->romimage.fp) fclose(s->romimage.fp);

    memory_destroy(s);
}

void e3ds_update_datetime(E3DS* s) {
    struct {
        u64 time;
        u64 systemtick;
        u32 unk[4];
    }* datetime = PTR(SHARED_PAGE + 0x20);

    // need time since 1900 in milliseconds

    auto timeval = time(nullptr);
#ifndef _WIN32
    auto tm = localtime(&timeval);
    timeval += tm->tm_gmtoff;
#endif

    datetime->time = (timeval + 2'208'988'800) * 1000;
    datetime->systemtick = s->sched.now;
    datetime->unk[0] = 0xffb0ff0;
}

void e3ds_run_frame(E3DS* s) {
    while (!s->frame_complete) {
        e3ds_restore_context(s);
        if (!s->cpu.halt) {
            while (true) {
                s64 cycles =
                    FIFO_peek(s->sched.event_queue).time - s->sched.now;
                if (cycles <= 0) break;
                s->sched.now += cpu_run(s, cycles);
                if (s->cpu.halt) break;
            }
        }
        e3ds_save_context(s);
        run_next_event(&s->sched);
        run_to_present(&s->sched);
        while (s->cpu.halt && !s->frame_complete) {
            run_next_event(&s->sched);
        }
        e3ds_update_datetime(s);
    }
    s->frame_complete = false;
}

// lengths are size of buffer, including null terminator
void convert_utf16(char* dst, size_t dstlen, u16* src, size_t srclen) {
    int dsti = 0;
    for (int i = 0; i < srclen; i++) {
        u16 c = src[i];
        if (c < BIT(7)) {
            if (dsti + 1 > dstlen) break;
            dst[dsti++] = c;
        } else if (c < BIT(11)) {
            if (dsti + 2 > dstlen) break;
            dst[dsti++] = 0xc0 | (c >> 6);
            dst[dsti++] = 0x80 | (c & MASK(6));
        } else {
            if (dsti + 3 > dstlen) break;
            dst[dsti++] = 0xe0 | (c >> 12);
            dst[dsti++] = 0x80 | (c >> 6 & MASK(6));
            dst[dsti++] = 0x80 | (c & MASK(6));
        }
    }
    dst[dstlen - 1] = '\0';
}