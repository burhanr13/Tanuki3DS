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
    bool mute;

    E3DS system;

    int videoscale;

} EmulatorState;

extern EmulatorState ctremu;

#define EMUNAME "Tanuki3DS"

void emulator_read_args(int argc, char** argv);

int emulator_init(int argc, char** argv);
void emulator_quit();

void emulator_reset();

#endif