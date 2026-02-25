#ifndef Y2R_H
#define Y2R_H

#include "kernel/thread.h"

#include "srv.h"

typedef struct {
    bool enableInterrupt;
    bool busy;
    KEvent transferend;

    struct {
        u32 addr;
        u32 pitch;
        u32 gap;
        u32 size;
    } srcY, srcU, srcV, dst;
    u32 inputFmt, outputFmt, rotation, blockMode;
    s16 width, height, alpha;
    struct {
        float y_a, r_v, g_v, g_u, b_u;
        float r_off, g_off, b_off;
    } coeffs;
} Y2RData;

DECL_PORT(y2r);

void y2r_do_conversion(E3DS* s);

#endif
