#ifndef MIC_H
#define MIC_H

#include "../srv.h"

typedef struct {
    u8 gain;
} MicData;

DECL_PORT(mic);

#endif
