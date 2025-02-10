#include "emulator.h"

#include <confuse.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <3ds.h>
#include <services/hid.h>

bool g_infologs = false;
EmulatorState ctremu;

void load_config() {
    cfg_opt_t opts[] = {
        CFG_BOOL("verbose_log", cfg_false, 0),
        CFG_BOOL("vsync", cfg_true, 0),
        CFG_INT("video_scale", 1, 0),
        CFG_BOOL("shaderjit", cfg_true, 0),
        CFG_INT("vsh_threads", 0, 0),
        CFG_BOOL("ubershader", cfg_false, 0),
        CFG_END(),
    };
    cfg_t* cfg = cfg_init(opts, 0);

    auto res = cfg_parse(cfg, "config.txt");

    g_infologs = cfg_getbool(cfg, "verbose_log");
    ctremu.vsync = cfg_getbool(cfg, "vsync");
    ctremu.videoscale = cfg_getint(cfg, "video_scale");
    if (ctremu.videoscale < 1) ctremu.videoscale = 1;
    cfg_setint(cfg, "video_scale", ctremu.videoscale);
    ctremu.shaderjit = cfg_getbool(cfg, "shaderjit");
    ctremu.vshthreads = cfg_getint(cfg, "vsh_threads");
    if (ctremu.vshthreads < 0) ctremu.vshthreads = 0;
    if (ctremu.vshthreads > MAX_VSH_THREADS)
        ctremu.vshthreads = MAX_VSH_THREADS;
    cfg_setint(cfg, "vsh_threads", ctremu.vshthreads);
    ctremu.ubershader = cfg_getbool(cfg, "ubershader");

    FILE* fp = fopen("config.txt", "w");
    if (fp) {
        cfg_print(cfg, fp);
        fclose(fp);
    }

    cfg_free(cfg);
}

void emulator_init() {
    mkdir("system", S_IRWXU);
    mkdir("system/savedata", S_IRWXU);
    mkdir("system/extdata", S_IRWXU);
    mkdir("system/sdmc", S_IRWXU);

    ctremu.videoscale = 1;
    ctremu.vsync = true;
    ctremu.shaderjit = true;
    ctremu.vshthreads = 0;

    load_config();
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
