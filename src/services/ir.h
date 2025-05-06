#ifndef SRV_IR_H
#define SRV_IR_H

#include "kernel/thread.h"

#include "srv.h"

typedef struct {
    KEvent connection_status;
} IRData;

DECL_PORT(ir_user);

#endif
