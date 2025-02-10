#ifndef BACKEND_ARM_H
#define BACKEND_ARM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <arm/arm_core.h>
#include <arm/jit/ir.h>
#include <arm/jit/jit.h>
#include <arm/jit/register_allocator.h>
#include <arm/media.h>
#include <arm/vfp.h>

void* backend_arm_generate_code(IRBlock* ir, RegAllocation* regalloc,
                                ArmCore* cpu);
JITFunc backend_arm_get_code(void* backend);
void backend_arm_patch_links(JITBlock* block);
void backend_arm_free(void* backend);
void backend_arm_disassemble(void* backend);

#ifdef __cplusplus
}
#endif

#endif
