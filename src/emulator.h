#ifndef EMULATOR_H
#define EMULATOR_H

#include "common.h"

#include "3ds.h"

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

    E3DS system;

} EmulatorState;

extern EmulatorState ctremu;

#define EMUNAME "Tanuki3DS"

void emulator_read_args(int argc, char** argv);
void emulator_set_rom(const char* filename);

void emulator_init();
void emulator_quit();

void emulator_reset();

#endif