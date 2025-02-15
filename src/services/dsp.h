#ifndef DSP_H
#define DSP_H

#include <kernel/thread.h>

#include "srv.h"

typedef struct {
    // there are 3 interrupts and 4 channels to register
    // events for
    KEvent* events[3][4];

    KEvent semEvent;
} DSPData;

DECL_PORT(dsp);

#endif
