#ifndef GSP_H
#define GSP_H

#include "../memory.h"
#include "../srv.h"
#include "../svc_types.h"
#include "../thread.h"

enum {
    GSPEVENT_PSC0,
    GSPEVENT_PSC1,
    GSPEVENT_VBLANK0,
    GSPEVENT_VBLANK1,
    GSPEVENT_PPF,
    GSPEVENT_P3D,
    GSPEVENT_DMA,
};

typedef struct {
    KEvent* event;
    KSharedMem sharedmem;

    FIFO(u32, 4) toplcdfbs;
    FIFO(u32, 4) botlcdfbs;
} GSPData;

DECL_PORT(gsp_gpu);

void gsp_handle_event(E3DS* s, u32 arg);
void gsp_handle_command(E3DS* s);

#endif
