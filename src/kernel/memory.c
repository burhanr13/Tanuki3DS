#include "memory.h"

#ifdef FASTMEM
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "3ds.h"
#include "common.h"
#include "emulator.h"

#include "svc_types.h"

#define PGROUNDDOWN(a) ((a) & ~(PAGE_SIZE - 1))
#define PGROUNDUP(a) (((a) + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1))

#ifdef FASTMEM
#ifndef __linux__
#define memfd_create(name, x)                                                  \
    ({                                                                         \
        int fd = open(name, O_RDWR | O_CREAT);                                 \
        unlink(name);                                                          \
        fd;                                                                    \
    })
#endif

void sigsegv_handler(int sig, siginfo_t* info, void* ucontext) {
    u8* addr = info->si_addr;
    if (ctremu.system.virtmem <= addr &&
        addr < ctremu.system.virtmem + BITL(32)) {
        lerror("(FATAL) invalid 3DS virtual memory access at %08x (pc near "
               "%08x, thread %d)",
               addr - ctremu.system.virtmem, ctremu.system.cpu.pc,
               ((KThread*) ctremu.system.process.handles[0])->id);
        cpu_print_state(&ctremu.system.cpu);
        exit(1);
    }
    if (ctremu.system.physmem <= addr &&
        addr < ctremu.system.physmem + BITL(32)) {
        lerror("(FATAL) invalid 3DS physical memory access at %08x",
               addr - ctremu.system.physmem);
        exit(1);
    }
    sigaction(sig, &(struct sigaction) {.sa_handler = SIG_DFL}, nullptr);
}
#endif

u32 physaddr2memoff(u32 paddr) {
    if (FCRAM_PBASE <= paddr && paddr < FCRAM_PBASE + FCRAM_SIZE) {
        return offsetof(E3DSMemory, fcram[paddr - FCRAM_PBASE]);
    }
    if (VRAM_PBASE <= paddr && paddr < VRAM_PBASE + VRAM_SIZE) {
        return offsetof(E3DSMemory, vram[paddr - VRAM_PBASE]);
    }
    if (DSPRAM_PBASE <= paddr && paddr < DSPRAM_PBASE + DSPRAM_SIZE) {
        return offsetof(E3DSMemory, dspram[paddr - DSPRAM_PBASE]);
    }
    lerror("unknown physical memory address %08x", paddr);
    cpu_print_state(&ctremu.system.cpu);
    exit(1);
}

void memory_init(E3DS* s) {
#ifdef FASTMEM
    s->mem_fd = memfd_create(".3dsram", 0);
    if (s->mem_fd < 0 || ftruncate(s->mem_fd, sizeof(E3DSMemory)) < 0) {
        perror("memfd_create");
        exit(1);
    }
    s->mem = mmap(nullptr, sizeof(E3DSMemory), PROT_READ | PROT_WRITE,
                  MAP_SHARED, s->mem_fd, 0);
#else
    s->mem = calloc(1, sizeof *s->mem);
#endif

#ifdef FASTMEM
    s->physmem = mmap(nullptr, BITL(32), PROT_NONE,
                      MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    s->virtmem = mmap(nullptr, BITL(32), PROT_NONE,
                      MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (s->physmem == MAP_FAILED || s->virtmem == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    s->cpu.fastmem = s->virtmem;
    s->gpu.mem = s->physmem;
    s->dsp.mem = s->physmem;

    struct sigaction sa = {.sa_sigaction = sigsegv_handler,
                           .sa_flags = SA_SIGINFO};
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);

    void* ptr =
        mmap(&s->physmem[FCRAM_PBASE], FCRAM_SIZE, PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_FIXED, s->mem_fd, offsetof(E3DSMemory, fcram));
    if (ptr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    ptr = mmap(&s->physmem[VRAM_PBASE], VRAM_SIZE, PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_FIXED, s->mem_fd, offsetof(E3DSMemory, vram));
    if (ptr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    ptr = mmap(&s->physmem[DSPRAM_PBASE], DSPRAM_SIZE, PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_FIXED, s->mem_fd, offsetof(E3DSMemory, dspram));
    if (ptr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
#else
    s->gpu.mem = s->mem;
    s->dsp.mem = s->mem;
#endif

    s->pheap.startpg = FCRAM_SIZE / PAGE_SIZE;
    s->pheap.endpg = 0;
    FCRAMHeapNode* linearnode = malloc(sizeof *linearnode);
    linearnode->startpg = 0;
    linearnode->endpg = 0;
    s->pheap.next = linearnode;
    linearnode->prev = &s->pheap;
    s->pheap.prev = linearnode;
    linearnode->next = &s->pheap;

    VMBlock* initblk = malloc(sizeof *initblk);
    *initblk = (VMBlock) {
        .startpg = 0, .endpg = BIT(20), .perm = 0, .state = MEMST_FREE};
    s->process.vmblocks.startpg = BIT(20);
    s->process.vmblocks.endpg = BIT(20);
    s->process.vmblocks.next = initblk;
    s->process.vmblocks.prev = initblk;
    initblk->prev = &s->process.vmblocks;
    initblk->next = &s->process.vmblocks;
}

void memory_destroy(E3DS* s) {
    while (s->pheap.next != &s->pheap) {
        auto tmp = s->pheap.next;
        s->pheap.next = s->pheap.next->next;
        free(tmp);
    }
    while (s->process.vmblocks.next != &s->process.vmblocks) {
        VMBlock* tmp = s->process.vmblocks.next;
        s->process.vmblocks.next = s->process.vmblocks.next->next;
        free(tmp);
    }
    for (int i = 0; i < BIT(10); i++) {
        free(s->process.ptab[i]);
    }

#ifdef FASTMEM
    sigaction(SIGSEGV, &(struct sigaction) {.sa_handler = SIG_DFL}, nullptr);
    sigaction(SIGBUS, &(struct sigaction) {.sa_handler = SIG_DFL}, nullptr);

    munmap(s->physmem, BITL(32));
    munmap(s->virtmem, BITL(32));
    munmap(s->mem, sizeof(E3DSMemory));

    close(s->mem_fd);
#else
    free(s->mem);
#endif
}

u32 memory_physalloc(E3DS* s, u32 size) {
    size = PGROUNDUP(size);
    u32 npage = PGROUNDUP(size) / PAGE_SIZE;

    auto cur = &s->pheap;
    do {
        u32 gap = cur->startpg - cur->prev->endpg;
        if (gap < npage) {
            cur = cur->prev;
            continue;
        }

        FCRAMHeapNode* n = malloc(sizeof *n);

        n->endpg = cur->startpg;
        n->startpg = n->endpg - npage;
        n->next = cur;
        n->prev = cur->prev;
        cur->prev = n;
        n->prev->next = n;

        u32 paddr = FCRAM_PBASE + n->startpg * PAGE_SIZE;

        linfo("allocating physical memory at %08x with size %x", paddr, size);

        return paddr;
    } while (cur != &s->pheap);

    lerror("ran out of physical memory");
    return 0;
}

PageEntry ptabread(PageTable ptab, u32 vaddr) {
    u32 l1 = (vaddr >> 22) & MASK(10);
    u32 l2 = (vaddr >> 12) & MASK(10);
    PageEntry res = {};
    res.state = MEMST_FREE;
    if (ptab[l1]) {
        res = ptab[l1][l2];
    }
    if (res.state == MEMST_FREE) {
        lerror("invalid virtual memory address %08x", vaddr);
        cpu_print_state(&ctremu.system.cpu);
        exit(1);
    }
    return res;
}

void ptabwrite(PageTable ptab, u32 vaddr, u32 paddr, u32 perm, u32 state) {
    u32 l1 = (vaddr >> 22) & MASK(10);
    u32 l2 = (vaddr >> 12) & MASK(10);

    if (!ptab[l1]) ptab[l1] = calloc(BIT(10), sizeof(PageEntry));
    ptab[l1][l2].paddr = paddr;
    ptab[l1][l2].perm = perm;
    ptab[l1][l2].state = state;
}

void* sw_pptr(E3DSMemory* m, u32 addr) {
    return &m->raw[physaddr2memoff(addr)];
}

void* sw_vptr(E3DS* s, u32 addr) {
    auto ent = ptabread(s->process.ptab, addr);
    return sw_pptr(s->mem, ent.paddr + addr % PAGE_SIZE);
}

void insert_vmblock(E3DS* s, u32 base, u32 size, u32 perm, u32 state) {
    VMBlock* n = malloc(sizeof(VMBlock));
    *n = (VMBlock) {.startpg = base >> 12,
                    .endpg = (base + size) >> 12,
                    .perm = perm,
                    .state = state};

    VMBlock* l = s->process.vmblocks.next;
    while (l != &s->process.vmblocks) {
        if (l->startpg <= n->startpg && n->startpg < l->endpg) break;
        l = l->next;
    }
    VMBlock* r = l->next;
    n->next = r;
    n->prev = l;
    l->next = n;
    r->prev = n;

    while (r->startpg < n->endpg) {
        if (r->endpg < n->endpg) {
            n->next = r->next;
            n->next->prev = n;
            free(r);
            r = n->next;
        } else {
            r->startpg = n->endpg;
            break;
        }
    }
    if (n->startpg < l->endpg) {
        if (n->endpg < l->endpg) {
            VMBlock* nr = malloc(sizeof(VMBlock));
            *nr = *l;
            nr->prev = n;
            nr->next = r;
            n->next = nr;
            r->prev = nr;
            nr->startpg = n->endpg;
            nr->endpg = l->endpg;
            r = nr;
        }
        if (l->startpg == n->startpg) {
            n->prev = l->prev;
            n->prev->next = n;
            free(l);
            l = n->prev;
        } else {
            l->endpg = n->startpg;
        }
    }
    if (r->startpg < BIT(20) && r->perm == n->perm && r->state == n->state) {
        n->endpg = r->endpg;
        n->next = r->next;
        n->next->prev = n;
        free(r);
    }
    if (l->startpg < BIT(20) && l->perm == n->perm && l->state == n->state) {
        l->endpg = n->endpg;
        l->next = n->next;
        l->next->prev = l;
        free(n);
    }
}

void print_vmblocks(VMBlock* vmblocks) {
    VMBlock* cur = vmblocks->next;
    while (cur != vmblocks) {
        printf("[%08x,%08x,%d,%d] ", cur->startpg << 12, cur->endpg << 12,
               cur->perm, cur->state);
        cur = cur->next;
    }
    printf("\n");
}

u32 memory_virtmap(E3DS* s, u32 paddr, u32 vaddr, u32 size, u32 perm,
                   u32 state) {
    vaddr = PGROUNDDOWN(vaddr);
    paddr = PGROUNDDOWN(paddr);
    size = PGROUNDUP(size);

    insert_vmblock(s, vaddr, size, perm, state);

    linfo("mapping virtual memory at %08x size %x from paddr %08x "
          "(perm=%d,state=%d)",
          vaddr, size, paddr, perm, state);

    u32 npage = size / PAGE_SIZE;

    for (int i = 0; i < npage; i++, vaddr += PAGE_SIZE, paddr += PAGE_SIZE) {
        ptabwrite(s->process.ptab, vaddr, paddr, perm, state);
#ifdef FASTMEM
        void* ptr =
            mmap(&s->virtmem[vaddr], PAGE_SIZE, PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_FIXED, s->mem_fd, physaddr2memoff(paddr));
        if (ptr == MAP_FAILED) {
            perror("mmap");
            exit(1);
        }
#endif
    }
    return vaddr;
}

u32 memory_virtmirror(E3DS* s, u32 srcvaddr, u32 dstvaddr, u32 size, u32 perm) {
    srcvaddr = PGROUNDDOWN(srcvaddr);
    dstvaddr = PGROUNDDOWN(dstvaddr);
    size = PGROUNDUP(size);

    insert_vmblock(s, dstvaddr, size, perm, MEMST_ALIAS);

    linfo("mirror mapping virtual memory at %08x size %x from vaddr %08x "
          "(perm=%d)",
          dstvaddr, size, srcvaddr, perm);

    u32 npage = size / PAGE_SIZE;

    for (int i = 0; i < npage;
         i++, srcvaddr += PAGE_SIZE, dstvaddr += PAGE_SIZE) {
        PageEntry ent = ptabread(s->process.ptab, srcvaddr);

        if (ent.state == MEMST_FREE) {
            lerror("invalid src address for mirrormap %08x", srcvaddr);
            return 0;
        }

        ptabwrite(s->process.ptab, dstvaddr, ent.paddr, perm, MEMST_ALIAS);
#ifdef FASTMEM
        void* ptr =
            mmap(&s->virtmem[dstvaddr], PAGE_SIZE, PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_FIXED, s->mem_fd, physaddr2memoff(ent.paddr));
        if (ptr == MAP_FAILED) {
            perror("mmap");
            exit(1);
        }
#endif
    }
    return dstvaddr;
}

u32 memory_virtalloc(E3DS* s, u32 addr, u32 size, u32 perm, u32 state) {
    size = PGROUNDUP(size);
    u32 paddr = memory_physalloc(s, size);
    memory_virtmap(s, paddr, addr, size, perm, state);
    s->process.used_memory += size;
    return addr;
}

u32 memory_linearheap_grow(E3DS* s, u32 size, u32 perm) {
    size = PGROUNDUP(size);

    auto linearblk = s->pheap.next;
    u32 npage = PGROUNDUP(size) / PAGE_SIZE;

    if (linearblk->endpg + npage > linearblk->next->startpg) {
        lerror("ran of physical memory");
        return 0;
    }
    u32 startaddr = LINEAR_HEAP_BASE + linearblk->endpg * PAGE_SIZE;
    linearblk->endpg += npage;
    linfo("extending linear heap by %x", size);
    memory_virtmap(s, FCRAM_PBASE, LINEAR_HEAP_BASE,
                   (linearblk->endpg - linearblk->startpg) * PAGE_SIZE, perm,
                   MEMST_CONTINUOUS);
    s->process.used_memory += size;
    return startaddr;
}

VMBlock* memory_virtquery(E3DS* s, u32 addr) {
    addr >>= 12;
    VMBlock* b = s->process.vmblocks.next;
    while (b != &s->process.vmblocks) {
        if (b->startpg <= addr && addr < b->endpg) return b;
        b = b->next;
    }
    return nullptr;
}

void sharedmem_alloc(E3DS* s, KSharedMem* shmem) {
    shmem->paddr = memory_physalloc(s, shmem->size);
}