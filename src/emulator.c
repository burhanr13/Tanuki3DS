#include "emulator.h"

#include <fcntl.h>
#include <sys/stat.h>

#include "3ds.h"
#include "config.h"

#ifdef _WIN32
#define mkdir(path, ...) mkdir(path)
#endif

bool g_infologs = false;
EmulatorState ctremu;

void emulator_init() {
    mkdir("system", S_IRWXU);
    mkdir("system/savedata", S_IRWXU);
    mkdir("system/extdata", S_IRWXU);
    mkdir("system/sdmc", S_IRWXU);

    ctremu.syncmode = SYNC_VIDEO;
    ctremu.videoscale = 1;
    ctremu.shaderjit = true;
#ifdef _WIN32
    ctremu.ubershader = true;
#else
    ctremu.hwvshaders = true;
#endif

    load_config();

    if (ctremu.videoscale < 1) ctremu.videoscale = 1;
    if (ctremu.vshthreads > MAX_VSH_THREADS)
        ctremu.vshthreads = MAX_VSH_THREADS;
    if (ctremu.vshthreads < 2) ctremu.vshthreads = 0;

    save_config();
}

void emulator_quit() {
    if (ctremu.initialized) {
        e3ds_destroy(&ctremu.system);
        ctremu.initialized = false;
    }

    free(ctremu.romfilenoext);
    free(ctremu.romfile);
    ctremu.romfile = nullptr;
    ctremu.romfilenodir = nullptr;
    ctremu.romfilenoext = nullptr;
}

void emulator_set_rom(const char* filename) {
    free(ctremu.romfile);
    ctremu.romfile = nullptr;
    free(ctremu.romfilenoext);
    ctremu.romfilenoext = nullptr;

    if (!filename) {
        ctremu.romfile = nullptr;
        return;
    }

    ctremu.romfile = strdup(filename);

    ctremu.romfilenodir = strrchr(ctremu.romfile, '/');
#ifdef _WIN32
    if (!ctremu.romfilenodir) {
        ctremu.romfilenodir = strrchr(ctremu.romfile, '\\');
    }
#endif
    if (ctremu.romfilenodir) ctremu.romfilenodir++;
    else ctremu.romfilenodir = ctremu.romfile;
    ctremu.romfilenoext = strdup(ctremu.romfilenodir);
    char* c = strrchr(ctremu.romfilenoext, '.');
    if (c) *c = '\0';
}

bool emulator_reset() {
    if (ctremu.initialized) {
        e3ds_destroy(&ctremu.system);
        ctremu.initialized = false;
    }

    if (!ctremu.romfile) return true;

    if (!e3ds_init(&ctremu.system, ctremu.romfile)) {
        emulator_set_rom(nullptr);
        return false;
    }

    ctremu.initialized = true;

    return true;
}
