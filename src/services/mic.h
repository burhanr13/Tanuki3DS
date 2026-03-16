#ifndef MIC_H
#define MIC_H

#include "kernel/thread.h"

#include "srv.h"

typedef struct {
    KEvent event;
    KSharedMem* shmem;
    u8 gain;
    bool sampling;
    u8 encoding;
    int sampleRate;
    bool loop;
} MICData;

DECL_PORT(mic);

void mic_send_data(E3DS* s, void* src, u32 srclen);

#endif
