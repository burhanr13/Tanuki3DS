#ifndef DSP_H
#define DSP_H

#include <kernel/thread.h>

#include "srv.h"

typedef struct {
    KEvent* event;
    KEvent semEvent;
} DSPData;

DECL_PORT(dsp);

#endif
