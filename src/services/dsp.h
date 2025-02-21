#ifndef SRV_DSP_H
#define SRV_DSP_H

#include "kernel/thread.h"

#include "srv.h"

typedef struct {
    // correspond to interrupt 2, pipes 2 and 3
    // the other ones aren't used i think
    KEvent* audio_event;
    KEvent* binary_event;

    KEvent sem_event; // signalled when there is new audio data
    u16 sem_mask;
} DSPData;

DECL_PORT(dsp);

#endif
