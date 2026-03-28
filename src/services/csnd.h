#ifndef CSND_H
#define CSND_H

#include "kernel/memory.h"

#include "srv.h"

typedef struct {
    KSharedMem shmem;
} CSNDData;

DECL_PORT(csnd);

#endif
