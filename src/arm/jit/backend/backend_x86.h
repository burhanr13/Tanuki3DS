#ifndef BACKEND_X86_H
#define BACKEND_X86_H

#include <ras/ras.h>

#include "arm/jit/jit.h"
#include "arm/jit/register_allocator.h"
#include "common.h"

typedef struct {
    rasLabel lab;
    u32 attrs, addr;
} X86LinkPatch;

typedef struct {
    rasBlock* code;

    RegAllocation* regalloc;
    HostRegAllocation hralloc;
    ArmCore* cpu;

    Vec(X86LinkPatch) links;
} X86CodeBackend;

X86CodeBackend* backend_x86_generate_code(IRBlock* ir, RegAllocation* regalloc,
                                ArmCore* cpu);
JITFunc backend_x86_get_code(X86CodeBackend* backend);
void backend_x86_patch_links(JITBlock* block);
void backend_x86_free(X86CodeBackend* backend);
void backend_x86_disassemble(X86CodeBackend* backend);

#endif
