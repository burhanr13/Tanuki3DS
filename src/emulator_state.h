#ifndef EMULATOR_STATE_H
#define EMULATOR_STATE_H

#include "3ds.h"
#include "common.h"

typedef struct {
    char* romfile;
    char* romfilenodir;

    bool initialized;
    bool running;
    bool uncap;
    bool pause;
    bool mute;

    HLE3DS system;

} EmulatorState;

extern EmulatorState ctremu;

#endif