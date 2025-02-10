#ifndef EMULATOR_H
#define EMULATOR_H

#include <3ds.h>
#include <common.h>

typedef struct {
    char* romfile;
    char* romfilenodir;
    char* romfilenoext;

    bool initialized;
    bool running;
    bool uncap;
    bool pause;

    bool vsync;
    int videoscale;
    bool shaderjit;
    int vshthreads;
    bool hwvshaders;
    bool ubershader;

    E3DS system;

} EmulatorState;

extern EmulatorState ctremu;

#define EMUNAME "Tanuki3DS"

void emulator_set_rom(const char* filename);

void emulator_init();
void emulator_quit();

void emulator_reset();

#endif