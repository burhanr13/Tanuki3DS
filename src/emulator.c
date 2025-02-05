#include "emulator.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "3ds.h"
#include "emulator.h"
#include "services/hid.h"

bool g_infologs = false;
EmulatorState ctremu;

void emulator_init() {
    mkdir("system", S_IRWXU);
    mkdir("system/savedata", S_IRWXU);
    mkdir("system/extdata", S_IRWXU);
    mkdir("system/sdmc", S_IRWXU);

    // config files ?
    ctremu.videoscale = 1;
    ctremu.vsync = true;
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
    free(ctremu.romfilenoext);

    ctremu.romfile = strdup(filename);

    ctremu.romfilenodir = strrchr(ctremu.romfile, '/');
    if (ctremu.romfilenodir) ctremu.romfilenodir++;
    else ctremu.romfilenodir = ctremu.romfile;
    ctremu.romfilenoext = strdup(ctremu.romfilenodir);
    char* c = strrchr(ctremu.romfilenoext, '.');
    if (c) *c = '\0';
}

void emulator_reset() {
    if (ctremu.initialized) {
        e3ds_destroy(&ctremu.system);
        ctremu.initialized = false;
    }

    if (!ctremu.romfile) return;

    e3ds_init(&ctremu.system, ctremu.romfile);

    ctremu.initialized = true;
}
