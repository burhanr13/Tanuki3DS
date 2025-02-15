#ifndef PORT_H
#define PORT_H

#include "common.h"
#include "kernel/ipc.h"
#include "kernel/kernel.h"

typedef struct _3DS E3DS;

void services_init(E3DS* s);

u32 srvobj_make_handle(E3DS* s, KObject* o);

DECL_PORT(srv);

DECL_PORT(errf);

#endif
