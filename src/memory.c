#include "memory.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "3ds.h"
#include "common.h"
#include "emulator_state.h"
#include "svc_types.h"

#ifdef __APPLE__
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
        lerror(
            "(FATAL) invalid 3DS virtual memory access at %08x (pc near %08x)",
            addr - ctremu.system.virtmem, ctremu.system.cpu.pc);
        cpu_print_state(&ctremu.system.cpu);
        exit(1);
    }
    if (ctremu.system.physmem <= addr &&
        addr < ctremu.system.physmem + BITL(32)) {
        lerror("(FATAL) invalid 3DS physical memory access at %08x",
               addr - ctremu.system.physmem);
        exit(1);
    }
    sigaction(sig, &(struct sigaction){.sa_handler = SIG_DFL}, NULL);
}

void hle3ds_memory_init(HLE3DS* s) {
    s->physmem = mmap(NULL, BITL(32), PROT_NONE,
                      MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    s->virtmem = mmap(NULL, BITL(32), PROT_NONE,
                      MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (s->physmem == MAP_FAILED || s->virtmem == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    s->gpu.mem = s->physmem;

    struct sigaction sa = {.sa_sigaction = sigsegv_handler,
                           .sa_flags = SA_SIGINFO};
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);

    s->fcram_fd = memfd_create(".fcram", 0);
    if (s->fcram_fd < 0 || ftruncate(s->fcram_fd, FCRAMSIZE) < 0) {
        perror("memfd_create");
        exit(1);
    }
    void* ptr =
        mmap(&s->physmem[FCRAM_PBASE], FCRAMSIZE, PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_FIXED, s->fcram_fd, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    s->vram_fd = memfd_create(".vram", 0);
    if (s->vram_fd < 0 || ftruncate(s->vram_fd, VRAMSIZE) < 0) {
        perror("memfd_create");
        exit(1);
    }
    ptr = mmap(&s->physmem[VRAM_PBASE], VRAMSIZE, PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_FIXED, s->vram_fd, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    ptr = mmap(&s->virtmem[VRAMBASE], VRAMSIZE, PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_FIXED, s->vram_fd, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    VMBlock* initblk = malloc(sizeof(VMBlock));
    *initblk = (VMBlock){
        .startpg = 0, .endpg = BIT(20), .perm = 0, .state = MEMST_FREE};
    s->process.vmblocks.startpg = BIT(20);
    s->process.vmblocks.endpg = BIT(20);
    s->process.vmblocks.next = initblk;
    s->process.vmblocks.prev = initblk;
    initblk->prev = &s->process.vmblocks;
    initblk->next = &s->process.vmblocks;
}

void hle3ds_memory_destroy(HLE3DS* s) {
    while (s->process.vmblocks.next != &s->process.vmblocks) {
        VMBlock* tmp = s->process.vmblocks.next;
        s->process.vmblocks.next = s->process.vmblocks.next->next;
        free(tmp);
    }

    sigaction(SIGSEGV, &(struct sigaction){.sa_handler = SIG_DFL}, NULL);
    sigaction(SIGBUS, &(struct sigaction){.sa_handler = SIG_DFL}, NULL);

    munmap(s->virtmem, BITL(32));

    close(s->fcram_fd);
    close(s->vram_fd);
}

void insert_vmblock(HLE3DS* s, VMBlock* n) {
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

void hle3ds_vmmap(HLE3DS* s, u32 base, u32 size, u32 perm, u32 state,
                  bool linear) {
    base = base & ~(PAGE_SIZE - 1);
    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (!size) return;

    VMBlock* n = malloc(sizeof(VMBlock));
    *n = (VMBlock){.startpg = base >> 12,
                   .endpg = (base + size) >> 12,
                   .perm = perm,
                   .state = state};
    insert_vmblock(s, n);

    void* ptr;
    if (linear) {
        ptr = mmap(&s->virtmem[base], size, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_FIXED, s->fcram_fd, 0);
    } else {
        ptr = mmap(&s->virtmem[base], size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    }
    if (ptr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    s->process.used_memory += size;
    linfo("mapped 3DS virtual memory at %08x with size 0x%x, perm %d, "
          "state %d",
          base, size, perm, state);
}

VMBlock* hle3ds_vmquery(HLE3DS* s, u32 addr) {
    addr >>= 12;
    VMBlock* b = s->process.vmblocks.next;
    while (b != &s->process.vmblocks) {
        if (b->startpg <= addr && addr < b->endpg) return b;
        b = b->next;
    }
    return NULL;
}