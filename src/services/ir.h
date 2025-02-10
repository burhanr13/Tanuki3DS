#ifndef SRV_IR_H
#define SRV_IR_H

#include "../srv.h"
#include "../thread.h"

typedef struct {
    KEvent event;
} IRData;

DECL_PORT(ir);

#endif
