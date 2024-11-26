#include "arm.h"

// #include <stdlib.h>

// #include "arm_core.h"
// #include "jit/jit.h"

ArmInstrFormat arm_lookup[BIT(8)][BIT(4)];

// this file contains an arm interpreter that has not been updated for 3DS
// the only relevant part is the decoder

// ArmExecFunc exec_funcs[] = {
//     [ARM_DATAPROC] = exec_arm_data_proc,
//     [ARM_PSRTRANS] = exec_arm_psr_trans,
//     [ARM_MULTIPLY] = exec_arm_multiply,
//     [ARM_MULTIPLYLONG] = exec_arm_multiply_long,
//     [ARM_MULTIPLYSHORT] = exec_arm_multiply_short,
//     [ARM_SWAP] = exec_arm_swap,
//     [ARM_BRANCHEXCH] = exec_arm_branch_exch,
//     [ARM_LEADINGZEROS] = exec_arm_leading_zeros,
//     [ARM_SATARITH] = exec_arm_sat_arith,
//     [ARM_HALFTRANS] = exec_arm_half_trans,
//     [ARM_SINGLETRANS] = exec_arm_single_trans,
//     [ARM_UNDEFINED] = exec_arm_undefined,
//     [ARM_BLOCKTRANS] = exec_arm_block_trans,
//     [ARM_BRANCH] = exec_arm_branch,
//     [ARM_CPREGTRANS] = exec_arm_cp_reg_trans,
//     [ARM_SWINTR] = exec_arm_sw_intr,
// };

// ArmExecFunc func_lookup[BIT(8)][BIT(4)];

__attribute__((constructor)) void arm_generate_lookup() {
    for (int dechi = 0; dechi < BIT(8); dechi++) {
        for (int declo = 0; declo < BIT(4); declo++) {
            arm_lookup[dechi][declo] =
                arm_decode_instr((ArmInstr){.dechi = dechi, .declo = declo});
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

// static inline bool eval_cond(ArmCore* cpu, ArmInstr instr) {
//     if (instr.cond == C_AL) return true;
//     switch (instr.cond) {
//         case C_EQ:
//             return cpu->cpsr.z;
//         case C_NE:
//             return !cpu->cpsr.z;
//         case C_CS:
//             return cpu->cpsr.c;
//         case C_CC:
//             return !cpu->cpsr.c;
//         case C_MI:
//             return cpu->cpsr.n;
//         case C_PL:
//             return !cpu->cpsr.n;
//         case C_VS:
//             return cpu->cpsr.v;
//         case C_VC:
//             return !cpu->cpsr.v;
//         case C_HI:
//             return cpu->cpsr.c && !cpu->cpsr.z;
//         case C_LS:
//             return !cpu->cpsr.c || cpu->cpsr.z;
//         case C_GE:
//             return cpu->cpsr.n == cpu->cpsr.v;
//         case C_LT:
//             return cpu->cpsr.n != cpu->cpsr.v;
//         case C_GT:
//             return !cpu->cpsr.z && (cpu->cpsr.n == cpu->cpsr.v);
//         case C_LE:
//             return cpu->cpsr.z || (cpu->cpsr.n != cpu->cpsr.v);
//         default:
//             return true;
//     }
// }

// void arm_exec_instr(ArmCore* cpu) {
//     if (cpu->pending_flush) {
//         cpu_flush(cpu);
//         cpu->pending_flush = false;
//     }

//     ArmInstr instr = cpu->cur_instr;

// #ifdef CPULOG
//     cpu->log[cpu->log_idx].addr = cpu->cur_instr_addr;
//     cpu->log[cpu->log_idx].instr = instr;
//     cpu->log_idx = (cpu->log_idx + 1) % LOGMAX;
// #endif

//     if (!eval_cond(cpu, instr)) {
//         cpu_fetch_instr(cpu);
//         return;
//     }

//     func_lookup[instr.dechi][instr.declo](cpu, instr);
// }

// static u32 arm_shifter(ArmCore* cpu, u8 shift, u32 operand, u32* carry) {
//     u32 shift_type = (shift >> 1) & 0b11;
//     u32 shift_amt = shift >> 3;
//     if (shift_amt) {
//         switch (shift_type) {
//             case S_LSL:
//                 *carry = (operand >> (32 - shift_amt)) & 1;
//                 return operand << shift_amt;
//             case S_LSR:
//                 *carry = (operand >> (shift_amt - 1)) & 1;
//                 return operand >> shift_amt;
//             case S_ASR:
//                 *carry = (operand >> (shift_amt - 1)) & 1;
//                 return (s32) operand >> shift_amt;
//             case S_ROR:
//                 *carry = (operand >> (shift_amt - 1)) & 1;
//                 return (operand >> shift_amt) | (operand << (32 -
//                 shift_amt));
//         }
//     } else {
//         switch (shift_type) {
//             case S_LSL:
//                 return operand;
//             case S_LSR:
//                 *carry = operand >> 31;
//                 return 0;
//             case S_ASR:
//                 *carry = operand >> 31;
//                 return (operand >> 31) ? -1 : 0;
//             case S_ROR:
//                 *carry = operand & 1;
//                 return (operand >> 1) | (cpu->cpsr.c << 31);
//         }
//     }
//     return 0;
// }

// DECL_ARM_EXEC(data_proc) {
//     u32 op1, op2;

//     u32 z, c = cpu->cpsr.c, n, v = cpu->cpsr.v;
//     if (instr.data_proc.i) {
//         if (cpu->cpsr.t) {
//             op2 = instr.data_proc.op2;
//         } else {
//             op2 = instr.data_proc.op2 & 0xff;
//             u32 shift_amt = instr.data_proc.op2 >> 8;
//             if (shift_amt) {
//                 shift_amt *= 2;
//                 c = (op2 >> (shift_amt - 1)) & 1;
//                 op2 = (op2 >> shift_amt) | (op2 << (32 - shift_amt));
//             }
//         }
//         op1 = cpu->r[instr.data_proc.rn];
//         cpu_fetch_instr(cpu);
//     } else {
//         u32 rm = instr.data_proc.op2 & 0b1111;
//         u32 shift = instr.data_proc.op2 >> 4;

//         if (shift & 1) {
//             cpu_fetch_instr(cpu);
//             op2 = cpu->r[rm];

//             u32 rs = shift >> 4;
//             u32 shift_amt = cpu->r[rs] & 0xff;

//             if (shift_amt >= 32) {
//                 switch ((shift >> 1) & 0b11) {
//                     case S_LSL:
//                         if (shift_amt == 32) c = op2 & 1;
//                         else c = 0;
//                         op2 = 0;
//                         break;
//                     case S_LSR:
//                         if (shift_amt == 32) c = op2 >> 31;
//                         else c = 0;
//                         op2 = 0;
//                         break;
//                     case S_ASR:
//                         if (op2 >> 31) {
//                             c = 1;
//                             op2 = -1;
//                         } else {
//                             c = 0;
//                             op2 = 0;
//                         }
//                         break;
//                     case S_ROR:
//                         shift_amt %= 32;
//                         c = (op2 >> (shift_amt - 1)) & 1;
//                         op2 = (op2 >> shift_amt) | (op2 << (32 - shift_amt));
//                         break;
//                 }
//             } else if (shift_amt > 0) {
//                 op2 =
//                     arm_shifter(cpu, (shift & 0b111) | shift_amt << 3, op2,
//                     &c);
//             }

//             op1 = cpu->r[instr.data_proc.rn];
//         } else {
//             op2 = arm_shifter(cpu, shift, cpu->r[rm], &c);
//             op1 = cpu->r[instr.data_proc.rn];
//             cpu_fetch_instr(cpu);
//         }
//     }
//     if (instr.data_proc.rn == 15 && instr.data_proc.rd != 15) op1 &= ~0b10;

//     if (instr.data_proc.s) {
//         u32 res = 0;
//         bool arith = false;
//         u32 car = 0;
//         u32 tmp;
//         bool save = true;
//         switch (instr.data_proc.opcode) {
//             case A_AND:
//                 res = op1 & op2;
//                 break;
//             case A_EOR:
//                 res = op1 ^ op2;
//                 break;
//             case A_SUB:
//                 arith = true;
//                 op2 = ~op2;
//                 car = 1;
//                 break;
//             case A_RSB:
//                 arith = true;
//                 tmp = op1;
//                 op1 = op2;
//                 op2 = ~tmp;
//                 car = 1;
//                 break;
//             case A_ADD:
//                 arith = true;
//                 break;
//             case A_ADC:
//                 arith = true;
//                 car = cpu->cpsr.c;
//                 break;
//             case A_SBC:
//                 arith = true;
//                 op2 = ~op2;
//                 car = cpu->cpsr.c;
//                 break;
//             case A_RSC:
//                 arith = true;
//                 tmp = op1;
//                 op1 = op2;
//                 op2 = ~tmp;
//                 car = cpu->cpsr.c;
//                 break;
//             case A_TST:
//                 res = op1 & op2;
//                 save = false;
//                 break;
//             case A_TEQ:
//                 res = op1 ^ op2;
//                 save = false;
//                 break;
//             case A_CMP:
//                 arith = true;
//                 op2 = ~op2;
//                 car = 1;
//                 save = false;
//                 break;
//             case A_CMN:
//                 arith = true;
//                 save = false;
//                 break;
//             case A_ORR:
//                 res = op1 | op2;
//                 break;
//             case A_MOV:
//                 res = op2;
//                 break;
//             case A_BIC:
//                 res = op1 & ~op2;
//                 break;
//             case A_MVN:
//                 res = ~op2;
//                 break;
//         }

//         if (arith) {
//             res = op1 + op2;
//             c = (op1 > res) || (res > res + car);
//             res += car;
//             v = (op1 >> 31) == (op2 >> 31) && (op1 >> 31) != (res >> 31);
//         }
//         z = res == 0;
//         n = res >> 31;

//         if (instr.data_proc.rd == 15) {
//             CpuMode mode = cpu->cpsr.m;
//             if (!(mode == M_USER || mode == M_SYSTEM)) {
//                 cpu->cpsr.w = cpu->spsr;
//                 cpu_update_mode(cpu, mode);
//             }
//         } else {
//             cpu->cpsr.z = z;
//             cpu->cpsr.n = n;
//             cpu->cpsr.c = c;
//             cpu->cpsr.v = v;
//         }
//         if (save) {
//             cpu->r[instr.data_proc.rd] = res;
//             if (instr.data_proc.rd == 15) cpu_flush(cpu);
//         }
//     } else {
//         u32 rd = instr.data_proc.rd;
//         switch (instr.data_proc.opcode) {
//             case A_AND:
//                 cpu->r[rd] = op1 & op2;
//                 break;
//             case A_EOR:
//                 cpu->r[rd] = op1 ^ op2;
//                 break;
//             case A_SUB:
//                 cpu->r[rd] = op1 - op2;
//                 break;
//             case A_RSB:
//                 cpu->r[rd] = op2 - op1;
//                 break;
//             case A_ADD:
//                 cpu->r[rd] = op1 + op2;
//                 break;
//             case A_ADC:
//                 cpu->r[rd] = op1 + op2 + cpu->cpsr.c;
//                 break;
//             case A_SBC:
//                 cpu->r[rd] = op1 - op2 - 1 + cpu->cpsr.c;
//                 break;
//             case A_RSC:
//                 cpu->r[rd] = op2 - op1 - 1 + cpu->cpsr.c;
//                 break;
//             case A_TST:
//                 return;
//             case A_TEQ:
//                 return;
//             case A_CMP:
//                 return;
//             case A_CMN:
//                 return;
//             case A_ORR:
//                 cpu->r[rd] = op1 | op2;
//                 break;
//             case A_MOV:
//                 cpu->r[rd] = op2;
//                 break;
//             case A_BIC:
//                 cpu->r[rd] = op1 & ~op2;
//                 break;
//             case A_MVN:
//                 cpu->r[rd] = ~op2;
//                 break;
//         }
//         if (rd == 15) cpu_flush(cpu);
//     }
// }

// DECL_ARM_EXEC(psr_trans) {
//     if (instr.psr_trans.op) {
//         u32 op2;
//         if (instr.psr_trans.i) {
//             op2 = instr.psr_trans.op2 & 0xff;
//             u32 rot = instr.psr_trans.op2 >> 8 << 1;
//             op2 = (op2 >> rot) | (op2 << (32 - rot));
//         } else {
//             u32 rm = instr.psr_trans.op2 & 0b1111;
//             op2 = cpu->r[rm];
//         }
//         u32 mask = 0;
//         if (instr.psr_trans.f) mask |= 0xff000000;
//         if (instr.psr_trans.s) mask |= 0x00ff0000;
//         if (instr.psr_trans.x) mask |= 0x0000ff00;
//         if (instr.psr_trans.c) mask |= 0x000000ff;
//         if (cpu->cpsr.m == M_USER) mask &= 0xf0000000;
//         op2 &= mask;
//         if (instr.psr_trans.p) {
//             cpu->spsr &= ~mask;
//             cpu->spsr |= op2;
//         } else {
//             CpuMode m = cpu->cpsr.m;
//             cpu->cpsr.w &= ~mask;
//             cpu->cpsr.w |= op2;
//             cpu_update_mode(cpu, m);
//         }
//     } else {
//         u32 psr;
//         if (instr.psr_trans.p) {
//             psr = cpu->spsr;
//         } else {
//             psr = cpu->cpsr.w;
//         }
//         cpu->r[instr.psr_trans.rd] = psr;
//     }
//     cpu_fetch_instr(cpu);
// }

// DECL_ARM_EXEC(multiply) {
//     cpu_fetch_instr(cpu);
//     u32 res = cpu->r[instr.multiply.rm] * cpu->r[instr.multiply.rs];
//     if (instr.multiply.a) {
//         res += cpu->r[instr.multiply.rn];
//     }
//     cpu->r[instr.multiply.rd] = res;
//     if (instr.multiply.s) {
//         cpu->cpsr.z = cpu->r[instr.multiply.rd] == 0;
//         cpu->cpsr.n = cpu->r[instr.multiply.rd] >> 31;
//     }
// }

// DECL_ARM_EXEC(multiply_long) {
//     cpu_fetch_instr(cpu);
//     u64 res;
//     if (instr.multiply_long.u) {
//         s64 sres;
//         sres = (s64) ((s32) cpu->r[instr.multiply_long.rm]) *
//                (s64) ((s32) cpu->r[instr.multiply_long.rs]);
//         res = sres;
//     } else {
//         res = (u64) cpu->r[instr.multiply_long.rm] *
//               (u64) cpu->r[instr.multiply_long.rs];
//     }
//     if (instr.multiply_long.a) {
//         res += (u64) cpu->r[instr.multiply_long.rdlo] |
//                ((u64) cpu->r[instr.multiply_long.rdhi] << 32);
//     }
//     if (instr.multiply_long.s) {
//         cpu->cpsr.z = res == 0;
//         cpu->cpsr.n = res >> 63;
//     }
//     cpu->r[instr.multiply_long.rdlo] = res;
//     cpu->r[instr.multiply_long.rdhi] = res >> 32;
// }

// DECL_ARM_EXEC(multiply_short) {
//     cpu_fetch_instr(cpu);

//     s64 op1 = (s16) (cpu->r[instr.multiply_short.rs] >>
//                      (16 * instr.multiply_short.y));
//     s64 op2;
//     if (instr.multiply_short.op == 0b01) {
//         op2 = (s32) cpu->r[instr.multiply_short.rm];
//     } else {
//         op2 = (s16) (cpu->r[instr.multiply_short.rm] >>
//                      (16 * instr.multiply_short.x));
//     }
//     s64 res = op1 * op2;
//     switch (instr.multiply_short.op) {
//         case 0:
//             res += (s32) cpu->r[instr.multiply_short.rn];
//             if (res > INT32_MAX || res < INT32_MIN) {
//                 cpu->cpsr.q = 1;
//             }
//             cpu->r[instr.multiply_short.rd] = res;
//             break;
//         case 1:
//             res >>= 16;
//             if (!instr.multiply_short.x) {
//                 res += (s32) cpu->r[instr.multiply_short.rn];
//                 if (res > INT32_MAX || res < INT32_MIN) {
//                     cpu->cpsr.q = 1;
//                 }
//             }
//             cpu->r[instr.multiply_short.rd] = res;
//             break;
//         case 2:
//             res += cpu->r[instr.multiply_short.rn];
//             res += (s64) cpu->r[instr.multiply_short.rd] << 32;
//             cpu->r[instr.multiply_short.rn] = res;
//             cpu->r[instr.multiply_short.rd] = res >> 32;
//             break;
//         case 3:
//             cpu->r[instr.multiply_short.rd] = res;
//             break;
//     }
// }

// DECL_ARM_EXEC(swap) {
//     u32 addr = cpu->r[instr.swap.rn];
//     cpu_fetch_instr(cpu);
//     if (instr.swap.b) {
//         u8 data = cpu->read8(cpu, addr, false);
//         cpu->write8(cpu, addr, cpu->r[instr.swap.rm]);
//         cpu->r[instr.swap.rd] = data;
//     } else {
//         u32 data = cpu->read32(cpu, addr);
//         cpu->write32(cpu, addr, cpu->r[instr.swap.rm]);
//         cpu->r[instr.swap.rd] = data;
//     }
// }

// DECL_ARM_EXEC(branch_exch) {
//     u32 dest = cpu->r[instr.branch_exch.rn];
//     if (instr.branch_exch.l) {
//         if (cpu->cpsr.t) {
//             cpu->lr = (cpu->pc - 2) | 1;
//         } else {
//             cpu->lr = cpu->pc - 4;
//         }
//     }
//     cpu_fetch_instr(cpu);
//     cpu->pc = dest;
//     cpu->cpsr.t = cpu->pc & 1;
//     cpu_flush(cpu);
// }

// DECL_ARM_EXEC(leading_zeros) {
//     u32 op = cpu->r[instr.leading_zeros.rm];
//     u32 ct = 0;
//     if (op == 0) ct = 32;
//     else
//         while (!(op & BIT(31))) {
//             op <<= 1;
//             ct++;
//         }
//     cpu->r[instr.leading_zeros.rd] = ct;
//     cpu_fetch_instr(cpu);
// }

// DECL_ARM_EXEC(sat_arith) {
//     s64 op1 = (s32) cpu->r[instr.sat_arith.rm];
//     s64 op2 = (s32) cpu->r[instr.sat_arith.rn];
//     if (instr.sat_arith.d) {
//         op2 *= 2;
//         if (op2 > INT32_MAX) {
//             op2 = INT32_MAX;
//             cpu->cpsr.q = 1;
//         }
//         if (op2 < INT32_MIN) {
//             op2 = INT32_MIN;
//             cpu->cpsr.q = 1;
//         }
//     }
//     s64 res = op1;
//     if (instr.sat_arith.op) res -= op2;
//     else res += op2;
//     if (res > INT32_MAX) {
//         res = INT32_MAX;
//         cpu->cpsr.q = 1;
//     }
//     if (res < INT32_MIN) {
//         res = INT32_MIN;
//         cpu->cpsr.q = 1;
//     }
//     cpu->r[instr.sat_arith.rd] = res;

//     cpu_fetch_instr(cpu);
// }

// DECL_ARM_EXEC(half_trans) {

//     u32 addr = cpu->r[instr.half_trans.rn];
//     u32 offset;
//     if (instr.half_trans.i) {
//         offset = instr.half_trans.offlo | (instr.half_trans.offhi << 4);
//     } else {
//         offset = cpu->r[instr.half_trans.offlo];
//     }
//     cpu_fetch_instr(cpu);

//     if (!instr.half_trans.u) offset = -offset;
//     u32 wback = addr + offset;
//     if (instr.half_trans.p) addr = wback;

//     if (instr.half_trans.s) {
//         if (instr.half_trans.l) {
//             if (instr.half_trans.w || !instr.half_trans.p) {
//                 cpu->r[instr.half_trans.rn] = wback;
//             }
//             if (instr.half_trans.h) {
//                 cpu->r[instr.half_trans.rd] = cpu->read16(cpu, addr, true);
//             } else {
//                 cpu->r[instr.half_trans.rd] = cpu->read8(cpu, addr, true);
//             }
//             if (instr.half_trans.rd == 15) cpu_flush(cpu);
//         } else {
//             if (instr.half_trans.h) {
//                 cpu->write32(cpu, addr & ~3, cpu->r[instr.half_trans.rd]);
//                 cpu->write32(cpu, (addr & ~3) + 4,
//                              cpu->r[instr.half_trans.rd + 1]);
//                 if (instr.half_trans.w || !instr.half_trans.p) {
//                     cpu->r[instr.half_trans.rn] = wback;
//                 }
//             } else {
//                 if (instr.half_trans.w || !instr.half_trans.p) {
//                     cpu->r[instr.half_trans.rn] = wback;
//                 }
//                 cpu->r[instr.half_trans.rd] = cpu->read32(cpu, addr & ~3);
//                 cpu->r[instr.half_trans.rd + 1] =
//                     cpu->read32(cpu, (addr & ~3) + 4);
//             }
//         }
//     } else if (instr.half_trans.h) {
//         if (instr.half_trans.l) {
//             if (instr.half_trans.w || !instr.half_trans.p) {
//                 cpu->r[instr.half_trans.rn] = wback;
//             }
//             cpu->r[instr.half_trans.rd] = cpu->read16(cpu, addr, false);
//             if (instr.half_trans.rd == 15) {
//                 cpu_flush(cpu);
//             }
//         } else {
//             cpu->write16(cpu, addr, cpu->r[instr.half_trans.rd]);
//             if (instr.half_trans.w || !instr.half_trans.p) {
//                 cpu->r[instr.half_trans.rn] = wback;
//             }
//         }
//     }
// }

// DECL_ARM_EXEC(single_trans) {
//     if (instr.cond == 0xf) {
//         cpu_fetch_instr(cpu);
//         return;
//     }

//     u32 addr = cpu->r[instr.single_trans.rn];
//     if (instr.single_trans.rn == 15) addr &= ~0b10;
//     u32 offset;
//     if (instr.single_trans.i) {
//         u32 rm = instr.single_trans.offset & 0b1111;
//         offset = cpu->r[rm];
//         u8 shift = instr.single_trans.offset >> 4;
//         u32 carry;
//         offset = arm_shifter(cpu, shift, offset, &carry);
//     } else {
//         offset = instr.single_trans.offset;
//     }

//     cpu_fetch_instr(cpu);

//     if (!instr.single_trans.u) offset = -offset;
//     u32 wback = addr + offset;
//     if (instr.single_trans.p) addr = wback;

//     if (instr.single_trans.b) {
//         if (instr.single_trans.l) {
//             if (instr.single_trans.w || !instr.single_trans.p) {
//                 cpu->r[instr.single_trans.rn] = wback;
//             }
//             cpu->r[instr.single_trans.rd] = cpu->read8(cpu, addr, false);
//             if (instr.single_trans.rd == 15) cpu_flush(cpu);
//         } else {
//             cpu->write8(cpu, addr, cpu->r[instr.single_trans.rd]);
//             if (instr.single_trans.w || !instr.single_trans.p) {
//                 cpu->r[instr.single_trans.rn] = wback;
//             }
//         }
//     } else {
//         if (instr.single_trans.l) {
//             if (instr.single_trans.w || !instr.single_trans.p) {
//                 cpu->r[instr.single_trans.rn] = wback;
//             }
//             cpu->r[instr.single_trans.rd] = cpu->read32(cpu, addr);
//             if (instr.single_trans.rd == 15) {
//                 cpu->cpsr.t = cpu->pc & 1;
//                 cpu_flush(cpu);
//             }
//         } else {
//             cpu->write32(cpu, addr, cpu->r[instr.single_trans.rd]);
//             if (instr.single_trans.w || !instr.single_trans.p) {
//                 cpu->r[instr.single_trans.rn] = wback;
//             }
//         }
//     }
// }

// DECL_ARM_EXEC(undefined) {
//     cpu_handle_exception(cpu, E_UND);
// }

// static u32* get_user_reg(ArmCore* cpu, int reg) {
//     if (reg < 8 || reg == 15) return &cpu->r[reg];
//     if (reg < 13) {
//         if (cpu->cpsr.m == M_FIQ) return &cpu->banked_r8_12[0][reg - 8];
//         else return &cpu->r[reg];
//     }
//     if (reg == 13) return &cpu->banked_sp[0];
//     if (reg == 14) return &cpu->banked_lr[0];
//     return NULL;
// }

// DECL_ARM_EXEC(block_trans) {
//     int rcount = 0;
//     int rlist[16];
//     u32 addr = cpu->r[instr.block_trans.rn];
//     u32 wback = addr;
//     if (instr.block_trans.rlist) {
//         for (int i = 0; i < 16; i++) {
//             if (instr.block_trans.rlist & BIT(i)) rlist[rcount++] = i;
//         }
//         if (instr.block_trans.u) {
//             wback += 4 * rcount;
//         } else {
//             wback -= 4 * rcount;
//             addr = wback;
//         }
//     } else {
//         rcount = 0;
//         if (instr.block_trans.u) {
//             wback += 0x40;
//         } else {
//             wback -= 0x40;
//             addr = wback;
//         }
//     }

//     if (instr.block_trans.p == instr.block_trans.u) addr += 4;
//     cpu_fetch_instr(cpu);

//     if (instr.block_trans.s &&
//         !((instr.block_trans.rlist & BIT(15)) && instr.block_trans.l)) {
//         if (instr.block_trans.l) {
//             for (int i = 0; i < rcount; i++) {
//                 if (i == rcount - 1 && instr.block_trans.w)
//                     cpu->r[instr.block_trans.rn] = wback;
//                 *get_user_reg(cpu, rlist[i]) =
//                     cpu->read32(cpu, (addr & ~3) + (i << 2));
//             }
//             if (rcount < 2 && instr.block_trans.w)
//                 cpu->r[instr.block_trans.rn] = wback;
//         } else {
//             for (int i = 0; i < rcount; i++) {
//                 cpu->write32(cpu, (addr & ~3) + (i << 2),
//                              *get_user_reg(cpu, rlist[i]));
//             }
//             if (instr.block_trans.w) cpu->r[instr.block_trans.rn] = wback;
//         }
//     } else {
//         if (instr.block_trans.l) {
//             if (cpu->cpsr.t) {
//                 if (instr.block_trans.w) cpu->r[instr.block_trans.rn] =
//                 wback; for (int i = 0; i < rcount; i++) {
//                     cpu->r[rlist[i]] = cpu->read32(cpu, (addr & ~3) + (i <<
//                     2));
//                 }
//             } else {
//                 for (int i = 0; i < rcount; i++) {
//                     if (i == rcount - 1 && instr.block_trans.w)
//                         cpu->r[instr.block_trans.rn] = wback;
//                     cpu->r[rlist[i]] = cpu->read32(cpu, (addr & ~3) + (i <<
//                     2));
//                 }
//                 if (rcount < 2 && instr.block_trans.w)
//                     cpu->r[instr.block_trans.rn] = wback;
//             }
//             if (instr.block_trans.rlist & BIT(15)) {
//                 cpu->cpsr.t = cpu->pc & 1;
//                 if (instr.block_trans.s) {
//                     CpuMode mode = cpu->cpsr.m;
//                     if (!(mode == M_USER || mode == M_SYSTEM)) {
//                         cpu->cpsr.w = cpu->spsr;
//                         cpu_update_mode(cpu, mode);
//                     }
//                 }
//                 cpu_flush(cpu);
//             }
//         } else {
//             for (int i = 0; i < rcount; i++) {
//                 cpu->write32(cpu, (addr & ~3) + (i << 2), cpu->r[rlist[i]]);
//             }
//             if (instr.block_trans.w) cpu->r[instr.block_trans.rn] = wback;
//         }
//     }
// }

// DECL_ARM_EXEC(branch) {
//     u32 offset = instr.branch.offset;
//     offset = (s32) (offset << 8) >> 8;
//     if (cpu->cpsr.t) offset <<= 1;
//     else offset <<= 2;
//     u32 dest = cpu->pc + offset;
//     if (instr.branch.l || instr.cond == 0xf) {
//         if (cpu->cpsr.t) {
//             if (offset & BIT(23)) {
//                 offset %= BIT(23);
//                 cpu->lr += offset;
//                 dest = cpu->lr;
//                 cpu->lr = (cpu->pc - 2) | 1;
//                 cpu_fetch_instr(cpu);
//                 if (instr.cond == 0xf) {
//                     cpu->cpsr.t = 0;
//                 }
//             } else {
//                 if (offset & BIT(22)) dest += 0xff800000;
//                 cpu->lr = dest;
//                 cpu_fetch_instr(cpu);
//                 return;
//             }
//         } else {
//             cpu->lr = (cpu->pc - 4) & ~0b11;
//             cpu_fetch_instr(cpu);
//             if (instr.cond == 0xf) {
//                 dest += instr.branch.l << 1;
//                 cpu->cpsr.t = 1;
//             }
//         }
//     }
//     cpu->pc = dest;
//     cpu_flush(cpu);
// }

// DECL_ARM_EXEC(cp_reg_trans) {
//     if (!cpu->cp15_read || !cpu->cp15_write) {
//         cpu_handle_exception(cpu, E_UND);
//         return;
//     }

//     cpu_fetch_instr(cpu);

//     if (instr.cp_reg_trans.cpnum == 15 && instr.cp_reg_trans.cpopc == 0) {
//         if (instr.cp_reg_trans.l) {
//             cpu->r[instr.cp_reg_trans.rd] =
//                 cpu->cp15_read(cpu, instr.cp_reg_trans.crn,
//                                instr.cp_reg_trans.crm,
//                                instr.cp_reg_trans.cp);
//         } else {
//             cpu->cp15_write(cpu, instr.cp_reg_trans.crn,
//             instr.cp_reg_trans.crm,
//                             instr.cp_reg_trans.cp,
//                             cpu->r[instr.cp_reg_trans.rd]);
//         }
//     }
// }

// DECL_ARM_EXEC(sw_intr) {
//     cpu_handle_exception(cpu, E_SWI);
// }

void arm_disassemble(ArmInstr instr, u32 addr, FILE* out) {

    static char* reg_names[16] = {"r0", "r1", "r2", "r3", "r4",  "r5",
                                  "r6", "r7", "r8", "r9", "r10", "r11",
                                  "ip", "sp", "lr", "pc"};
    static char* cond_names[16] = {"eq", "ne", "hs", "lo", "mi", "pl",
                                   "vs", "vc", "hi", "ls", "ge", "lt",
                                   "gt", "le", "",   ""};
    static char* alu_names[16] = {"and", "eor", "sub", "rsb", "add", "adc",
                                  "sbc", "rsc", "tst", "teq", "cmp", "cmn",
                                  "orr", "mov", "bic", "mvn"};
    static char* shift_names[5] = {"lsl", "lsr", "asr", "ror", "rrx"};

    char* cond = cond_names[instr.cond];

    switch (arm_decode_instr(instr)) {
        case ARM_SWINTR:
            fprintf(out, "swi%s 0x%x", cond, instr.sw_intr.arg);
            break;
        case ARM_BRANCH: {
            u32 off = instr.branch.offset;
            if (off & BIT(23)) off |= 0xff000000;
            if (instr.cond == 0xf) {
                fprintf(out, "blx 0x%x",
                        addr + 8 * (off << 2) + (instr.branch.l << 1));
            } else {
                fprintf(out, "b%s%s 0x%x", instr.branch.l ? "l" : "", cond,
                        addr + 8 + (off << 2));
            }
            break;
        }
        case ARM_CPREGTRANS:
            fprintf(out, "%s%s p%d, %d, %s, c%d, c%d, %d",
                    instr.cp_reg_trans.l ? "mrc" : "mcr",
                    cond_names[instr.cond], instr.cp_reg_trans.cpnum,
                    instr.cp_reg_trans.cpopc, reg_names[instr.cp_reg_trans.rd],
                    instr.cp_reg_trans.crn, instr.cp_reg_trans.crm,
                    instr.cp_reg_trans.cp);
            break;
        case ARM_BLOCKTRANS:
            if (instr.block_trans.rn == 13 &&
                (instr.block_trans.u != instr.block_trans.p) &&
                instr.block_trans.u == instr.block_trans.l &&
                instr.block_trans.w) {
                fprintf(out, "%s%s {", instr.block_trans.l ? "pop" : "push",
                        cond);
            } else {
                fprintf(out, "%s%s%s%s %s%s, {",
                        instr.block_trans.l ? "ldm" : "stm",
                        instr.block_trans.u ? "i" : "d",
                        instr.block_trans.p ? "b" : "a", cond,
                        reg_names[instr.block_trans.rn],
                        instr.block_trans.w ? "!" : "");
            }
            u16 rlist = instr.block_trans.rlist;
            for (int i = 0; i < 16; i++) {
                if (rlist & 1) {
                    fprintf(out, "%s", reg_names[i]);
                    rlist >>= 1;
                    if (rlist) {
                        fprintf(out, ", ");
                    }
                } else rlist >>= 1;
            }
            fprintf(out, "}%s", instr.block_trans.s ? "^" : "");
            break;
        case ARM_UNDEFINED:
            fprintf(out, "undefined");
            break;
        case ARM_SINGLETRANS:
            if (instr.cond == 0xf) {
                fprintf(out, "pld");
                return;
            }

            if (!instr.single_trans.i && instr.single_trans.rn == 15 &&
                instr.single_trans.p) {
                fprintf(out, "%s%s%s %s, [",
                        instr.single_trans.l ? "ldr" : "str",
                        instr.single_trans.b ? "b" : "", cond,
                        reg_names[instr.single_trans.rd]);

                u32 offset = instr.single_trans.offset;
                if (!instr.single_trans.u) offset = -offset;
                fprintf(out, "0x%x", addr + 8 + offset);
                fprintf(out, "]%s", instr.single_trans.w ? "!" : "");
            } else if (instr.single_trans.rn == 13 &&
                       instr.single_trans.offset == 4 &&
                       (!instr.single_trans.p || instr.single_trans.w) &&
                       !instr.single_trans.b && !instr.single_trans.i &&
                       instr.single_trans.l == instr.single_trans.u &&
                       instr.single_trans.u != instr.single_trans.p) {
                fprintf(out, "%s%s %s", instr.single_trans.l ? "pop" : "push",
                        cond, reg_names[instr.single_trans.rd]);
            } else {
                fprintf(out, "%s%s%s %s, [%s",
                        instr.single_trans.l ? "ldr" : "str",
                        instr.single_trans.b ? "b" : "", cond,
                        reg_names[instr.single_trans.rd],
                        reg_names[instr.single_trans.rn]);
                if (!instr.single_trans.p) {
                    fprintf(out, "]");
                }
                if (instr.single_trans.i) {
                    u32 rm = instr.single_trans.offset & 0b1111;
                    u32 shift = instr.single_trans.offset >> 4;
                    fprintf(out, ", %s%s", instr.single_trans.u ? "" : "-",
                            reg_names[rm]);
                    if (shift) {
                        u32 shift_type = (shift >> 1) & 0b11;
                        u32 shift_amt = (shift >> 3) & 0b11111;
                        if (!shift_amt) {
                            if (shift_type == S_ROR) {
                                shift_type = 4;
                                shift_amt = 1;
                            } else {
                                shift_amt = 32;
                            }
                        }
                        fprintf(out, ", %s #%d", shift_names[shift_type],
                                shift_amt);
                    }
                } else if (instr.single_trans.offset) {
                    fprintf(out, ", #%s0x%x", instr.single_trans.u ? "" : "-",
                            instr.single_trans.offset);
                }
                if (instr.single_trans.p) {
                    fprintf(out, "]%s", instr.single_trans.w ? "!" : "");
                }
            }
            break;
        case ARM_LEADINGZEROS:
            fprintf(out, "clz %s, %s", reg_names[instr.leading_zeros.rd],
                    reg_names[instr.leading_zeros.rm]);
            break;
        case ARM_BRANCHEXCH:
            fprintf(out, "%s%s %s", instr.branch_exch.l ? "blx" : "bx", cond,
                    reg_names[instr.branch_exch.rn]);
            break;
        case ARM_SWAP:
            fprintf(out, "swap%s%s %s, %s, [%s]", instr.swap.b ? "b" : "", cond,
                    reg_names[instr.swap.rd], reg_names[instr.swap.rm],
                    reg_names[instr.swap.rn]);
            break;
        case ARM_SATARITH:
            fprintf(out, "q%s%s %s, %s, %s", instr.sat_arith.d ? "d" : "",
                    instr.sat_arith.op ? "sub" : "add",
                    reg_names[instr.sat_arith.rd],
                    reg_names[instr.sat_arith.rm],
                    reg_names[instr.sat_arith.rn]);
            break;
        case ARM_MULTIPLYSHORT: {
            char* smul_names[] = {"smla", "smulw", "smlal", "smul"};
            fprintf(out, "%s", smul_names[instr.multiply_short.op]);
            if (instr.multiply_short.op != 1)
                fprintf(out, "%s", instr.multiply_short.x ? "t" : "b");
            fprintf(out, "%s %s, %s, %s, %s",
                    instr.multiply_short.y ? "t" : "b",
                    reg_names[instr.multiply_short.rd],
                    reg_names[instr.multiply_short.rn],
                    reg_names[instr.multiply_short.rs],
                    reg_names[instr.multiply_short.rm]);
            break;
        }
        case ARM_MULTIPLY:
            if (instr.multiply.a) {
                fprintf(
                    out, "mla%s%s %s, %s, %s, %s", instr.multiply.s ? "s" : "",
                    cond, reg_names[instr.multiply.rd],
                    reg_names[instr.multiply.rm], reg_names[instr.multiply.rs],
                    reg_names[instr.multiply.rn]);
            } else {
                fprintf(out, "mul%s%s %s, %s, %s", instr.multiply.s ? "s" : "",
                        cond, reg_names[instr.multiply.rd],
                        reg_names[instr.multiply.rm],
                        reg_names[instr.multiply.rs]);
            }
            break;
        case ARM_MULTIPLYLONG:
            fprintf(out, "%s%s%s%s %s, %s, %s, %s",
                    instr.multiply_long.u ? "s" : "u",
                    instr.multiply_long.a ? "mlal" : "mull",
                    instr.multiply_long.s ? "s" : "", cond,
                    reg_names[instr.multiply_long.rdlo],
                    reg_names[instr.multiply_long.rdhi],
                    reg_names[instr.multiply_long.rm],
                    reg_names[instr.multiply_long.rs]);
            break;
        case ARM_HALFTRANS:
            if (instr.half_trans.i && instr.half_trans.rn == 15 &&
                instr.half_trans.p) {
                fprintf(out, "%s%s%s%s %s, [",
                        instr.half_trans.l ? "ldr" : "str",
                        instr.half_trans.s ? "s" : "",
                        instr.half_trans.h ? "h" : "b", cond,
                        reg_names[instr.half_trans.rd]);

                u32 offset =
                    instr.half_trans.offlo | (instr.half_trans.offhi << 4);
                if (!instr.half_trans.u) offset = -offset;
                fprintf(out, "0x%x", addr + 8 + offset);
                fprintf(out, "]%s", instr.half_trans.w ? "!" : "");
            } else {
                if (!instr.half_trans.l && instr.half_trans.s) {
                    fprintf(out, "%s", instr.half_trans.h ? "strd" : "ldrd");
                } else {
                    fprintf(out, "%s%s%s", instr.half_trans.l ? "ldr" : "str",
                            instr.half_trans.s ? "s" : "",
                            instr.half_trans.h ? "h" : "b");
                }
                fprintf(out, "%s %s, [%s", cond, reg_names[instr.half_trans.rd],
                        reg_names[instr.half_trans.rn]);
                if (!instr.half_trans.p) {
                    fprintf(out, "]");
                }
                if (instr.half_trans.i) {
                    u32 offset =
                        instr.half_trans.offlo | (instr.half_trans.offhi << 4);
                    if (offset) {
                        fprintf(out, ", #%s0x%x", instr.half_trans.u ? "" : "-",
                                offset);
                    }
                } else {
                    fprintf(out, ", %s%s", instr.half_trans.u ? "" : "-",
                            reg_names[instr.half_trans.offlo]);
                }
                if (instr.half_trans.p) {
                    fprintf(out, "]%s", instr.half_trans.w ? "!" : "");
                }
            }
            break;
        case ARM_PSRTRANS:
            if (instr.psr_trans.op) {
                fprintf(out, "msr%s %s_%s%s, ", cond,
                        instr.psr_trans.p ? "spsr" : "cpsr",
                        instr.psr_trans.c ? "c" : "",
                        instr.psr_trans.f ? "f" : "");
                if (instr.psr_trans.i) {
                    u32 op2 = instr.psr_trans.op2 & 0xff;
                    u32 rot = (instr.psr_trans.op2 >> 8) << 1;
                    op2 = (op2 >> rot) | (op2 << (32 - rot));
                    fprintf(out, "#0x%x", op2);
                } else {
                    fprintf(out, "%s", reg_names[instr.psr_trans.op2 & 0xf]);
                }
            } else {
                fprintf(out, "mrs%s %s, %s", cond,
                        reg_names[instr.psr_trans.rd],
                        instr.psr_trans.p ? "spsr" : "cpsr");
            }
            break;
        case ARM_DATAPROC:
            if (instr.data_proc.i && instr.data_proc.rn == 15 &&
                (instr.data_proc.opcode == A_ADD ||
                 instr.data_proc.opcode == A_SUB)) {
                fprintf(out, "adr%s%s %s, [", instr.data_proc.s ? "s" : "",
                        cond, reg_names[instr.data_proc.rd]);
                u32 offset = instr.data_proc.op2 & 0xff;
                u32 rot = (instr.data_proc.op2 >> 8) << 1;
                offset = (offset >> rot) | (offset << (32 - rot));
                if (instr.data_proc.opcode == A_SUB) offset = -offset;
                fprintf(out, "0x%x]", addr + 8 + offset);
            } else {

                if (instr.data_proc.opcode >> 2 == 0b10) {
                    fprintf(out, "%s%s %s", alu_names[instr.data_proc.opcode],
                            cond, reg_names[instr.data_proc.rn]);
                } else {
                    fprintf(out, "%s%s%s %s", alu_names[instr.data_proc.opcode],
                            instr.data_proc.s ? "s" : "", cond,
                            reg_names[instr.data_proc.rd]);
                    if (!(instr.data_proc.opcode == A_MOV ||
                          instr.data_proc.opcode == A_MVN ||
                          instr.data_proc.rd == instr.data_proc.rn)) {
                        fprintf(out, ", %s", reg_names[instr.data_proc.rn]);
                    }
                }

                if (instr.data_proc.i) {
                    u32 op2 = instr.data_proc.op2 & 0xff;
                    u32 rot = (instr.data_proc.op2 >> 8) << 1;
                    fprintf(out, ", #0x%x", (op2 >> rot) | (op2 << (32 - rot)));
                } else {
                    u32 rm = instr.data_proc.op2 & 0b1111;
                    u32 shift = instr.data_proc.op2 >> 4;
                    fprintf(out, ", %s", reg_names[rm]);
                    u32 shift_type = (shift >> 1) & 0b11;
                    if (shift & 1) {
                        u32 shift_reg = shift >> 4;
                        fprintf(out, ", %s %s", shift_names[shift_type],
                                reg_names[shift_reg]);
                    } else if (shift) {
                        u32 shift_amt = (shift >> 3) & 0b11111;
                        if (!shift_amt) {
                            if (shift_type == S_ROR) {
                                shift_type = 4;
                                shift_amt = 1;
                            } else {
                                shift_amt = 32;
                            }
                        }
                        fprintf(out, ", %s #%d", shift_names[shift_type],
                                shift_amt);
                    }
                }
            }
            break;
        default:
            break;
    }
}
