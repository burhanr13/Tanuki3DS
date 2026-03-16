#ifndef RECOMPILER_H
#define RECOMPILER_H

#include "arm/arm.h"
#include "arm/arm_core.h"
#include "common.h"

#include "ir.h"

void compile_block(ArmCore* cpu, IRBlock* block, u32 start_addr);

bool arm_compile_instr(IRBlock* block, ArmCore* cpu, u32 addr, ArmInstr instr);

typedef bool (*ArmCompileFunc)(IRBlock*, ArmCore*, u32, ArmInstr);
extern ArmCompileFunc compile_funcs[ARM_MAX];

#endif
