#ifndef MEMORY_H
#define MEMORY_H

#include "common.h"
#include "kernel.h"

typedef struct _3DS E3DS;

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
    u32 size;
    bool mapped;

    void* defaultdata;
    u32 defaultdatalen;
} KSharedMem;

void e3ds_memory_init(E3DS* s);
void e3ds_memory_destroy(E3DS* s);

void e3ds_vmmap(E3DS* s, u32 base, u32 size, u32 perm, u32 state, bool linear);
VMBlock* e3ds_vmquery(E3DS* s, u32 addr);

#endif
