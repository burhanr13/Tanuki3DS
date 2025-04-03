#ifndef BACKEND_ARM_H
#define BACKEND_ARM_H

#include <ras/ras.h>

#include "../jit.h"
#include "../register_allocator.h"
#include "common.h"

typedef struct {
    rasLabel lab;
    u32 attrs, addr;
} LinkPatch;

typedef struct {
    rasBlock* code;

    RegAllocation* regalloc;
    HostRegAllocation hralloc;
    ArmCore* cpu;

    LinkPatch links[MAX_BLOCK_INSTRS];
    int nlinks;
} ArmCodeBackend;

ArmCodeBackend* backend_arm_generate_code(IRBlock* ir, RegAllocation* regalloc,
                                          ArmCore* cpu);
JITFunc backend_arm_get_code(ArmCodeBackend* backend);
void backend_arm_patch_links(JITBlock* block);
void backend_arm_free(ArmCodeBackend* backend);
void backend_arm_disassemble(ArmCodeBackend* backend);

#endif
