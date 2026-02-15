#include "emulator.h"

#include <SDL3/SDL_scancode.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "3ds.h"
#include "config.h"

#ifdef _WIN32
#define mkdir(path, ...) mkdir(path)
#endif

bool g_infologs = false;
EmulatorState ctremu;

void emulator_load_default_settings() {
    ctremu.windowW = 800;
    ctremu.windowH = 600;
    ctremu.vsync = false;
    ctremu.videoscale = 1;
    ctremu.vshthreads = 0;
    ctremu.shaderjit = true;
    ctremu.hwvshaders = true;
    ctremu.safeShaderMul = true;
    ctremu.ubershader = false;
    ctremu.hashTextures = true;
    ctremu.audiosync = true;
    ctremu.volume = 100;
    strcpy(ctremu.username, "ctremu");
    ctremu.language = 1;
    ctremu.region = 1;

    ctremu.inputmap.kb.a = SDL_SCANCODE_L;
    ctremu.inputmap.kb.b = SDL_SCANCODE_K;
    ctremu.inputmap.kb.x = SDL_SCANCODE_O;
    ctremu.inputmap.kb.y = SDL_SCANCODE_I;
    ctremu.inputmap.kb.l = SDL_SCANCODE_Q;
    ctremu.inputmap.kb.r = SDL_SCANCODE_P;
    ctremu.inputmap.kb.start = SDL_SCANCODE_RETURN;
    ctremu.inputmap.kb.select = SDL_SCANCODE_RSHIFT;
    ctremu.inputmap.kb.du = SDL_SCANCODE_UP;
    ctremu.inputmap.kb.dd = SDL_SCANCODE_DOWN;
    ctremu.inputmap.kb.dl = SDL_SCANCODE_LEFT;
    ctremu.inputmap.kb.dr = SDL_SCANCODE_RIGHT;
    ctremu.inputmap.kb.cu = SDL_SCANCODE_W;
    ctremu.inputmap.kb.cd = SDL_SCANCODE_S;
    ctremu.inputmap.kb.cl = SDL_SCANCODE_A;
    ctremu.inputmap.kb.cr = SDL_SCANCODE_D;
    ctremu.inputmap.kb.cmod = SDL_SCANCODE_LSHIFT;
    ctremu.inputmap.kb.cmodscale = 0.5f;

    ctremu.inputmap.freecam.ml = SDL_SCANCODE_A;
    ctremu.inputmap.freecam.mr = SDL_SCANCODE_D;
    ctremu.inputmap.freecam.mf = SDL_SCANCODE_W;
    ctremu.inputmap.freecam.mb = SDL_SCANCODE_S;
    ctremu.inputmap.freecam.mu = SDL_SCANCODE_R;
    ctremu.inputmap.freecam.md = SDL_SCANCODE_F;
    ctremu.inputmap.freecam.lu = SDL_SCANCODE_UP;
    ctremu.inputmap.freecam.ld = SDL_SCANCODE_DOWN;
    ctremu.inputmap.freecam.ll = SDL_SCANCODE_LEFT;
    ctremu.inputmap.freecam.lr = SDL_SCANCODE_RIGHT;
    ctremu.inputmap.freecam.rl = SDL_SCANCODE_Q;
    ctremu.inputmap.freecam.rr = SDL_SCANCODE_E;
    ctremu.inputmap.freecam.slow_mod = SDL_SCANCODE_LSHIFT;
    ctremu.inputmap.freecam.fast_mod = SDL_SCANCODE_RSHIFT;
}

void emulator_init() {
    mkdir("3ds", S_IRWXU);
    mkdir("3ds/savedata", S_IRWXU);
    mkdir("3ds/extdata", S_IRWXU);
    mkdir("3ds/sys_files", S_IRWXU);
    mkdir("3ds/sdmc", S_IRWXU);
    mkdir("3ds/sdmc/3ds", S_IRWXU);
    // homebrew needs this file to exist but the contents dont matter for hle
    // audio
    FILE* fp;
    if ((fp = fopen("3ds/sdmc/3ds/dspfirm.cdc", "wx"))) fclose(fp);

    emulator_load_default_settings();

    load_config();

    if (ctremu.viewlayout < LAYOUT_DEFAULT || ctremu.viewlayout >= LAYOUT_MAX)
        ctremu.viewlayout = LAYOUT_DEFAULT;
    if (ctremu.videoscale < 1) ctremu.videoscale = 1;
    if (ctremu.vshthreads > MAX_VSH_THREADS)
        ctremu.vshthreads = MAX_VSH_THREADS;
    if (ctremu.vshthreads < 0) ctremu.vshthreads = 0;
    if (ctremu.volume < 0) ctremu.volume = 0;
    if (ctremu.volume > 200) ctremu.volume = 200;

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

    save_config();
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

    int i;
    for (i = 0; i < HISTORYLEN; i++) {
        if (ctremu.history[i] && !strcmp(ctremu.history[i], filename)) break;
    }
    if (i == HISTORYLEN) i = HISTORYLEN - 1;
    free(ctremu.history[i]);
    memmove(&ctremu.history[1], &ctremu.history[0], i * sizeof(char*));
    ctremu.history[0] = strdup(ctremu.romfile);

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

void emulator_calc_viewports() {
    switch (ctremu.viewlayout) {
        case LAYOUT_DEFAULT: {

            int h = ctremu.windowH / 2;
            int wt = h * SCREEN_WIDTH_TOP / SCREEN_HEIGHT;
            int wb = h * SCREEN_WIDTH_BOT / SCREEN_HEIGHT;
            int xt = (ctremu.windowW - wt) / 2;
            int xb = (ctremu.windowW - wb) / 2;

            ctremu.screens[SCREEN_TOP] = (Rect) {xt, 0, wt, h};
            ctremu.screens[SCREEN_BOT] = (Rect) {xb, h, wb, h};

            break;
        }
        case LAYOUT_HORIZONTAL: {

            int wt = ctremu.windowW * SCREEN_WIDTH_TOP /
                     (SCREEN_WIDTH_TOP + SCREEN_WIDTH_BOT);
            int wb = ctremu.windowW - wt;
            int h = wt * SCREEN_HEIGHT / SCREEN_WIDTH_TOP;
            int y = (ctremu.windowH - h) / 2;

            ctremu.screens[SCREEN_TOP] = (Rect) {0, y, wt, h};
            ctremu.screens[SCREEN_BOT] = (Rect) {wt, y, wb, h};

            break;
        }
        case LAYOUT_LARGETOP: {

            int ht = ctremu.windowH;
            int wt = ht * SCREEN_WIDTH_TOP / SCREEN_HEIGHT;
            if (wt >= 4 * ctremu.windowW / 5) {
                wt = 4 * ctremu.windowW / 5;
                ht = wt * SCREEN_HEIGHT / SCREEN_WIDTH_TOP;
            }
            int wb = ctremu.windowW - wt;
            int hb = wb * SCREEN_HEIGHT / SCREEN_WIDTH_BOT;
            int yt = (ctremu.windowH - ht) / 2;
            int yb = yt + ht - hb;

            ctremu.screens[SCREEN_TOP] = (Rect) {0, yt, wt, ht};
            ctremu.screens[SCREEN_BOT] = (Rect) {wt, yb, wb, hb};

            break;
        }
        default:
            break;
    }
}
