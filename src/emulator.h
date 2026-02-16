#ifndef EMULATOR_H
#define EMULATOR_H

#include <cglm/cglm.h>
#include <setjmp.h>

#include "3ds.h"
#include "common.h"

#ifndef EMUVERSION
#define EMUVERSION ""
#endif

typedef void (*EmuAudioCallback)(s16 (*samples)[2], u32 num);

#define HISTORYLEN 10

typedef enum {
    LAYOUT_DEFAULT,
    LAYOUT_HORIZONTAL,
    LAYOUT_LARGETOP,

    LAYOUT_MAX
} ViewLayout;

typedef struct {
    int x, y, w, h;
} Rect;

typedef struct {
    char* romfile;
    char* romfilenodir;
    char* romfilenoext;

    char* history[HISTORYLEN];

    bool initialized;
    bool running;
    bool fastforward;
    bool pause;
    bool mute;

    bool vsync;
    bool audiosync;
    int videoscale;
    bool shaderjit;
    int vshthreads;
    bool hwvshaders;
    bool safeShaderMul;
    bool ubershader;
    bool hashTextures;

    ViewLayout viewlayout;
    Rect screens[2];
    int windowW, windowH;

    float volume;

    mat4 freecam_mtx;
    bool freecam_enable;

    char username[0x1c];
    int language;
    int region;

    struct {
        struct {
            int a, b, x, y, l, r, start, select;
            int cl, cr, cu, cd;
            int cmod;
            int dl, dr, du, dd;
            float cmodscale;
        } kb;
        struct {
            int ml, mr, mf, mb, mu, md;
            int lu, ld, ll, lr, rl, rr;
            int slow_mod, fast_mod;
        } freecam;
    } inputmap;

    EmuAudioCallback audio_cb;

    jmp_buf exceptionJmp;

    bool needs_swkbd;

    E3DS system;

} EmulatorState;

extern EmulatorState ctremu;

void emulator_set_rom(const char* filename);

void emulator_init();
void emulator_quit();

bool emulator_reset();

void emulator_calc_viewports();

void emulator_load_default_settings();

#endif
