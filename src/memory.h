#ifndef MEMORY_H
#define MEMORY_H

#include "common.h"
#include "kernel.h"

typedef struct _3DS HLE3DS;

typedef struct _VMBlock {
    u32 startpg;
    u32 endpg;
    u32 perm;
    u32 state;
    struct _VMBlock* next;
    struct _VMBlock* prev;
} VMBlock;

typedef struct {
    KObject hdr;

    u32 vaddr;
    bool mapped;
} KSharedMem;

void hle3ds_memory_init(HLE3DS* s);
void hle3ds_memory_destroy(HLE3DS* s);

void hle3ds_vmmap(HLE3DS* s, u32 base, u32 size, u32 perm, u32 state,
                  bool linear);
VMBlock* hle3ds_vmquery(HLE3DS* s, u32 addr);

#endif
