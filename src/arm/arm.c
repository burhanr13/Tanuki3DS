#include "arm.h"

ArmInstrFormat arm_lookup[BIT(8)][BIT(4)];

[[gnu::constructor]]
void arm_generate_lookup() {
    for (int dechi = 0; dechi < BIT(8); dechi++) {
        for (int declo = 0; declo < BIT(4); declo++) {
            arm_lookup[dechi][declo] =
                arm_decode_instr((ArmInstr) {.dechi = dechi, .declo = declo});
            // func_lookup[dechi][declo] = exec_funcs[arm_lookup[dechi][declo]];
        }
    }
}

ArmInstrFormat arm_decode_instr(ArmInstr instr) {
    if (instr.sw_intr.c1 == 0b1111) {
        return ARM_SWINTR;
    } else if (instr.cp_reg_trans.c1 == 0b1110 && instr.cp_reg_trans.c2 == 1) {
        return ARM_CPREGTRANS;
    } else if (instr.cp_data_proc.c1 == 0b1110 && instr.cp_reg_trans.c2 == 0) {
        return ARM_CPDATAPROC;
    } else if (instr.cp_double_reg_trans.c1 == 0b1100010) {
        return ARM_CPDOUBLEREGTRANS;
    } else if (instr.cp_data_trans.c1 == 0b110) {
        return ARM_CPDATATRANS;
    } else if (instr.branch.c1 == 0b101) {
        return ARM_BRANCH;
    } else if (instr.block_trans.c1 == 0b100) {
        return ARM_BLOCKTRANS;
    } else if (instr.pack_sat.c1 == 0b01101 && instr.pack_sat.c2 == 1) {
        return ARM_PACKSAT;
    } else if (instr.parallel_arith.c1 == 0b01100 &&
               instr.parallel_arith.c2 == 1) {
        return ARM_PARALLELARITH;
    } else if (instr.multiply_extra.c1 == 0b01110 &&
               instr.multiply_extra.c2 == 1) {
        return ARM_MULTIPLYEXTRA;
    } else if (instr.undefined.c1 == 0b011 && instr.undefined.c2 == 1) {
        return ARM_UNDEFINED;
    } else if (instr.single_trans.c1 == 0b01) {
        return ARM_SINGLETRANS;
    } else if (instr.leading_zeros.c1 == 0b00010110 &&
               instr.leading_zeros.c4 == 0b0001) {
        return ARM_LEADINGZEROS;
    } else if (instr.sat_arith.c1 == 0b00010 && instr.sat_arith.c2 == 0 &&
               instr.sat_arith.c4 == 0b0101) {
        return ARM_SATARITH;
    } else if (instr.branch_exch.c1 == 0b00010010 &&
               instr.branch_exch.c3 == 0b00 && instr.branch_exch.c4 == 1) {
        return ARM_BRANCHEXCH;
    } else if (instr.swap.c1 == 0b00010 && instr.swap.c2 == 0b00 &&
               instr.swap.c4 == 0b1001) {
        return ARM_SWAP;
    } else if (instr.multiply_short.c1 == 0b00010 &&
               instr.multiply_short.c2 == 0 && instr.multiply_short.c3 == 1 &&
               instr.multiply_short.c4 == 0) {
        return ARM_MULTIPLYSHORT;
    } else if (instr.multiply.c1 == 0b000000 && instr.multiply.c2 == 0b1001) {
        return ARM_MULTIPLY;
    } else if (instr.multiply_long.c1 == 0b0000 &&
               instr.multiply_long.c2 == 0b1001) {
        return ARM_MULTIPLYLONG;
    } else if (instr.half_trans.c1 == 0b000 && instr.half_trans.c2 == 1 &&
               instr.half_trans.c3 == 1) {
        return ARM_HALFTRANS;
    } else if (instr.psr_trans.c1 == 0b00 && instr.psr_trans.c2 == 0b10 &&
               instr.psr_trans.c3 == 0) {
        if (instr.psr_trans.i || !(instr.psr_trans.op2 & 0x0f0))
            return ARM_PSRTRANS;
        else return ARM_UNDEFINED;
    } else if (instr.data_proc.c1 == 0b00) {
        return ARM_DATAPROC;
    } else {
        return ARM_UNDEFINED;
    }
}
