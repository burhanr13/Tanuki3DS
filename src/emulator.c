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

const char usage[] = "ctremu [options] [romfile]\n"
                     "-h -- print help\n"
                     "-l -- enable info logging\n"
                     "-v -- disable vsync\n"
                     "-sN -- upscale by N\n";

void emulator_init() {
    if (!ctremu.romfile) {
        eprintf(usage);
        exit(1);
    }

    mkdir("system", S_IRWXU);
    mkdir("system/savedata", S_IRWXU);
    mkdir("system/extdata", S_IRWXU);
    mkdir("system/sdmc", S_IRWXU);

    emulator_reset();

}

void emulator_quit() {
    e3ds_destroy(&ctremu.system);

    free(ctremu.romfilenoext);
    free(ctremu.romfile);
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
    }

    e3ds_init(&ctremu.system, ctremu.romfile);

    ctremu.initialized = true;
}

void emulator_read_args(int argc, char** argv) {
    ctremu.videoscale = 1;
    ctremu.vsync = true;

    char c;
    while ((c = getopt(argc, argv, "hlvs:")) != -1) {
        switch (c) {
            case 'l':
                g_infologs = true;
                break;
            case 's': {
                int scale = atoi(optarg);
                if (scale <= 0) eprintf("invalid scale factor");
                else ctremu.videoscale = scale;
                break;
            }
            case 'v': {
                ctremu.vsync = false;
                break;
            }
            case '?':
            case 'h':
            default:
                eprintf(usage);
                exit(0);
        }
    }
    argc -= optind;
    argv += optind;
    if (argc >= 1) {
        emulator_set_rom(argv[0]);
    }
}
