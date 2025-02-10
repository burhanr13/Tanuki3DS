#ifndef Y2R_H
#define Y2R_H

#include <kernel/thread.h>

#include "srv.h"

typedef struct {
    bool enableInterrupt;
    KEvent transferend;
} Y2RData;

DECL_PORT(y2r);

#endif
