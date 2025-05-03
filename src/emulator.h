#ifndef EMULATOR_H
#define EMULATOR_H

#include <cglm/cglm.h>
#include <setjmp.h>

#include "3ds.h"
#include "common.h"

typedef void (*EmuAudioCallback)(s16 (*samples)[2], u32 num);

typedef struct {
    char* romfile;
    char* romfilenodir;
    char* romfilenoext;

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

    mat4 freecam_mtx;
    bool freecam_enable;

    EmuAudioCallback audio_cb;

    jmp_buf exceptionJmp;

    E3DS system;

} EmulatorState;

extern EmulatorState ctremu;

void emulator_set_rom(const char* filename);

void emulator_init();
void emulator_quit();

bool emulator_reset();

#endif