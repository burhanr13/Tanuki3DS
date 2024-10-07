#ifndef PROCESS_H
#define PROCESS_H

#include "kernel.h"
#include "memory.h"
#include "thread.h"

typedef struct _KProcess {
    KObject hdr;

    VMBlock vmblocks;

    KObject* handles[HANDLE_MAX];

    KThread* threads[THREAD_MAX];

    u32 used_memory;
} KProcess;

#endif