#ifndef Y2R_H
#define Y2R_H

#include "../srv.h"
#include "../thread.h"

typedef struct {
    bool enableInterrupt;
    KEvent transferend;
} Y2RData;

DECL_PORT(y2r);

#endif
