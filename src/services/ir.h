#ifndef SRV_IR_H
#define SRV_IR_H

#include "kernel/thread.h"

#include "srv.h"

typedef struct {
    KEvent event;
} IRData;

DECL_PORT(ir);

#endif
