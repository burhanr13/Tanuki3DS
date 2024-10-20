#ifndef RECOMPILER_H
#define RECOMPILER_H

#include "../../common.h"
#include "../arm.h"
#include "../arm_core.h"
#include "ir.h"

void compile_block(ArmCore* cpu, IRBlock* block, u32 start_addr);

bool arm_compile_instr(IRBlock* block, ArmCore* cpu, u32 addr, ArmInstr instr);

typedef bool (*ArmCompileFunc)(IRBlock*, ArmCore*, u32, ArmInstr);

#define DECL_ARM_COMPILE(f)                                                    \
    bool compile_arm_##f(IRBlock* block, ArmCore* cpu, u32 addr, ArmInstr instr)

DECL_ARM_COMPILE(data_proc);
DECL_ARM_COMPILE(psr_trans);
DECL_ARM_COMPILE(multiply);
DECL_ARM_COMPILE(multiply_long);
DECL_ARM_COMPILE(multiply_short);
DECL_ARM_COMPILE(swap);
DECL_ARM_COMPILE(branch_exch);
DECL_ARM_COMPILE(leading_zeros);
DECL_ARM_COMPILE(sat_arith);
DECL_ARM_COMPILE(half_trans);
DECL_ARM_COMPILE(single_trans);
DECL_ARM_COMPILE(pack_sat);
DECL_ARM_COMPILE(parallel_arith);
DECL_ARM_COMPILE(undefined);
DECL_ARM_COMPILE(block_trans);
DECL_ARM_COMPILE(branch);
DECL_ARM_COMPILE(cp_double_reg_trans);
DECL_ARM_COMPILE(cp_data_trans);
DECL_ARM_COMPILE(cp_data_proc);
DECL_ARM_COMPILE(cp_reg_trans);
DECL_ARM_COMPILE(sw_intr);

#endif
