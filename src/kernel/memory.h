#ifndef MEMORY_H
#define MEMORY_H

#include "common.h"

#include "kernel.h"

#define PAGE_SIZE BIT(12)

#define FCRAM_PBASE 0x2000'0000
#define FCRAM_SIZE (128 * BIT(20))
#define VRAM_PBASE 0x1800'0000
#define VRAM_SIZE (6 * BIT(20))
#define DSPRAM_PBASE (0x1ff0'0000)
#define DSPRAM_SIZE (512 * BIT(10))

#define LINEAR_HEAP_BASE 0x1400'0000

typedef union {
    struct {
        u8 fcram[FCRAM_SIZE];
        u8 vram[VRAM_SIZE];
        u8 dspram[DSPRAM_SIZE];
        u8 nullpage[PAGE_SIZE];
    };
    u8 raw[];
} E3DSMemory;

typedef struct _3DS E3DS;

typedef struct _FreeListNode {
    u32 startpg;
    u32 endpg;

    struct _FreeListNode *next, *prev;
} FreeListNode;

typedef struct {
    u32 paddr;
    u16 perm;
    u16 state;
} PageEntry;

typedef PageEntry* PageTable[BIT(10)];

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

    u32 paddr;
    u32 mapaddr;
    u32 size;
} KSharedMem;

void* sw_pptr(E3DSMemory* m, u32 addr);
void* sw_vptr(E3DS* s, u32 addr);

#ifdef FASTMEM
#define PPTR(addr) ((void*) &s->physmem[addr])
#define PTR(addr) ((void*) &s->virtmem[addr])
#else
#define PPTR(addr) sw_pptr(s->mem, addr)
#define PTR(addr) sw_vptr(s, addr)
#endif

void memory_init(E3DS* s);
void memory_destroy(E3DS* s);

u32 memory_physalloc(E3DS* s, u32 size);

u32 memory_virtmap(E3DS* s, u32 paddr, u32 vaddr, u32 size, u32 perm,
                   u32 state);
u32 memory_virtmirror(E3DS* s, u32 srcvaddr, u32 dstvaddr, u32 size, u32 perm);
u32 memory_virtalloc(E3DS* s, u32 addr, u32 size, u32 perm, u32 state);
u32 memory_linearheap_grow(E3DS* s, u32 size, u32 perm);
VMBlock* memory_virtquery(E3DS* s, u32 addr);

void sharedmem_alloc(E3DS* s, KSharedMem* shmem);


#define FCRAMUSERSIZE (96 * BIT(20))

#define STACK_BASE 0x1000'0000

#define VRAM_VBASE 0x1f00'0000
#define DSPRAM_VBASE 0x1ff0'0000

#define CONFIG_MEM 0x1ff80000
#define SHARED_PAGE 0x1ff81000

#define TLS_BASE 0x1ff82000
#define TLS_SIZE 0x200
#define IPC_CMD_OFF 0x80

static inline bool is_valid_physmem(u32 addr) {
    return (VRAM_PBASE <= addr && addr < VRAM_PBASE + VRAM_SIZE) ||
           (FCRAM_PBASE <= addr && addr < FCRAM_PBASE + FCRAM_SIZE);
}

static inline bool is_vram_addr(u32 addr) {
    return VRAM_PBASE <= addr && addr < VRAM_PBASE + VRAM_SIZE;
}

static inline u32 vaddr_to_paddr(u32 vaddr) {
    if (vaddr >= VRAM_VBASE) {
        return vaddr - VRAM_VBASE + VRAM_PBASE;
    }
    return vaddr - LINEAR_HEAP_BASE + FCRAM_PBASE;
}

#endif
