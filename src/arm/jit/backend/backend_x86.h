#ifndef BACKEND_X86_H
#define BACKEND_X86_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../ir.h"
#include "../jit.h"
#include "../register_allocator.h"
#include "../../arm_core.h"
#include "../../media.h"
#include "../../vfp.h"

void* backend_x86_generate_code(IRBlock* ir, RegAllocation* regalloc,
                                ArmCore* cpu);
JITFunc backend_x86_get_code(void* backend);
void backend_x86_patch_links(JITBlock* block);
void backend_x86_free(void* backend);
void backend_x86_disassemble(void* backend);

#ifdef __cplusplus
}
#endif

#endif
