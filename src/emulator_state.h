#ifndef EMULATOR_STATE_H
#define EMULATOR_STATE_H

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

    HLE3DS system;

} EmulatorState;

extern EmulatorState ctremu;

#endif