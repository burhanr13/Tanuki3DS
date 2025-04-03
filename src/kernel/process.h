#ifndef PROCESS_H
#define PROCESS_H

#include "kernel.h"
#include "memory.h"
#include "thread.h"

typedef struct _KProcess {
    KObject hdr;

    PageTable ptab;

    VMBlock vmblocks;

    KObject* handles[HANDLE_MAX];

    u32 nexttid;

    u32 allocTls;

    u32 used_memory;
} KProcess;

#endif