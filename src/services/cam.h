#ifndef CAM_H
#define CAM_H

#include "kernel/thread.h"

#include "srv.h"

typedef struct {
    KEvent recvEvent;
    KEvent vsyncEvent;
    KEvent errEvent;

    bool capturing;

    u32 width;
    u32 height;
    bool rgb;

    bool trimming;
    s16 x0, y0, x1, y1;

    u32 dstAddr;
} CAMData;

DECL_PORT(cam);

void cam_send_data(E3DS* s, void* src);

#endif
