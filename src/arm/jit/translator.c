#include "translator.h"

#include "../arm.h"
#include "../thumb.h"
#include "jit.h"
#include <stdlib.h>

ArmCompileFunc compile_funcs[ARM_MAX] = {
    [ARM_DATAPROC] = compile_arm_data_proc,
    [ARM_PSRTRANS] = compile_arm_psr_trans,
    [ARM_MULTIPLY] = compile_arm_multiply,
    [ARM_MULTIPLYLONG] = compile_arm_multiply_long,
    [ARM_MULTIPLYSHORT] = compile_arm_multiply_short,
    [ARM_SWAP] = compile_arm_swap,
    [ARM_BRANCHEXCH] = compile_arm_branch_exch,
    [ARM_LEADINGZEROS] = compile_arm_leading_zeros,
    [ARM_SATARITH] = compile_arm_sat_arith,
    [ARM_PACKSAT] = compile_arm_pack_sat,
    [ARM_PARALLELARITH] = compile_arm_parallel_arith,
    [ARM_HALFTRANS] = compile_arm_half_trans,
    [ARM_SINGLETRANS] = compile_arm_single_trans,
    [ARM_UNDEFINED] = compile_arm_undefined,
    [ARM_BLOCKTRANS] = compile_arm_block_trans,
    [ARM_BRANCH] = compile_arm_branch,
    [ARM_CPDOUBLEREGTRANS] = compile_arm_cp_double_reg_trans,
    [ARM_CPDATATRANS] = compile_arm_cp_data_trans,
    [ARM_CPDATAPROC] = compile_arm_cp_data_proc,
    [ARM_CPREGTRANS] = compile_arm_cp_reg_trans,
    [ARM_SWINTR] = compile_arm_sw_intr,
};

#define INSTRLEN (cpu->cpsr.t ? 2 : 4)

#define EMITXX(opc, _op1, _op2, _imm1, _imm2)                                  \
    (irblock_write(block, (IRInstr){.opcode = IR_##opc,                        \
                                    .imm1 = _imm1,                             \
                                    .imm2 = _imm2,                             \
                                    .op1 = _op1,                               \
                                    .op2 = _op2}))

#define EMITVX(opc, op1, op2, imm) EMITXX(opc, op1, op2, 0, imm)
#define EMITVV(opc, op1, op2) EMITVX(opc, op1, op2, 0)
#define EMITVI(opc, op1, op2) EMITVX(opc, op1, op2, 1)
#define EMITIX(opc, op1, op2, imm) EMITXX(opc, op1, op2, 1, imm)
#define EMITIV(opc, op1, op2) EMITIX(opc, op1, op2, 0)
#define EMITXV(opc, op1, op2, imm) EMITXX(opc, op1, op2, imm, 0)
#define EMIT0X(opc, op2, imm) EMITXX(opc, 0, op2, 1, imm)
#define EMIT0V(opc, op2) EMIT0X(opc, op2, 0)
#define EMIT0I(opc, op2) EMIT0X(opc, op2, 1)
#define EMITX0(opc, op1, imm) EMITXX(opc, op1, 0, imm, 1)
#define EMITV0(opc, op1) EMITX0(opc, op1, 0)
#define EMITI0(opc, op1) EMITX0(opc, op1, 1)
#define EMITII(opc, op1, op2) EMITXX(opc, op1, op2, 1, 1)
#define EMIT00(opc) EMITXX(opc, 0, 0, 1, 1)

#define EMIT_LOAD_REG(rn)                                                      \
    (rn == 15 ? EMIT0I(MOV, addr + 2 * INSTRLEN) : EMITI0(LOAD_REG, rn))
#define EMITX_STORE_REG(rn, op, imm) EMITIX(STORE_REG, rn, op, imm)
#define EMITV_STORE_REG(rn, op) EMITX_STORE_REG(rn, op, 0)
#define EMITI_STORE_REG(rn, op) EMITX_STORE_REG(rn, op, 1)

#define LASTV (block->code.size - 1)

#define EMIT_ALIGN_PC()                                                        \
    (EMITI0(LOAD_REG, 15), EMITVI(AND, LASTV, cpu->cpsr.t ? ~1 : ~3),          \
     EMITIV(STORE_REG, 15, LASTV))

void compile_block(ArmCore* cpu, IRBlock* block, u32 start_addr) {

    block->start_addr = start_addr;
    block->numinstr = 0;

    u32 addr = start_addr;

    EMIT00(BEGIN);

    for (int i = 0; i < MAX_BLOCK_INSTRS; i++) {
        ArmInstr instr = cpu->cpsr.t ? thumb_lookup[cpu->fetch16(cpu, addr)]
                                     : (ArmInstr){cpu->fetch32(cpu, addr)};
        bool can_continue = arm_compile_instr(block, cpu, addr, instr);
        addr += INSTRLEN;
        block->numinstr++;
#ifdef DEBUG_PC
        EMITI_STORE_REG(15, addr);
#endif
        if (!can_continue) break;
    }
    EMITI_STORE_REG(15, addr);
    EMIT00(END_RET);
    block->end_addr = addr;
}

u32 compile_cond(IRBlock* block, ArmInstr instr) {
    switch (instr.cond & ~1) {
        case C_EQ:
            EMITI0(LOAD_FLAG, ZF);
            break;
        case C_CS:
            EMITI0(LOAD_FLAG, CF);
            break;
        case C_MI:
            EMITI0(LOAD_FLAG, NF);
            break;
        case C_VS:
            EMITI0(LOAD_FLAG, VF);
            break;
        case C_HI: {
            u32 tmp = EMITI0(LOAD_FLAG, CF);
            EMITI0(LOAD_FLAG, ZF);
            EMIT0V(NOT, LASTV);
            EMITVV(AND, tmp, LASTV);
            break;
        }
        case C_GE: {
            u32 tmp = EMITI0(LOAD_FLAG, NF);
            EMITI0(LOAD_FLAG, VF);
            EMITVV(XOR, tmp, LASTV);
            instr.cond ^= 1;
            break;
        }
        case C_GT: {
            u32 tmp = EMITI0(LOAD_FLAG, NF);
            EMITI0(LOAD_FLAG, VF);
            tmp = EMITVV(XOR, tmp, LASTV);
            EMITI0(LOAD_FLAG, ZF);
            EMITVV(OR, tmp, LASTV);
            instr.cond ^= 1;
            break;
        }
    }
    if (instr.cond & 1) {
        EMITVI(JNZ, LASTV, 0);
    } else {
        EMITVI(JZ, LASTV, 0);
    }
    return LASTV;
}

bool arm_compile_instr(IRBlock* block, ArmCore* cpu, u32 addr, ArmInstr instr) {
    ArmCompileFunc func = compile_funcs[arm_lookup[instr.dechi][instr.declo]];
    if (instr.cond < C_AL) {
        u32 jmpaddr = compile_cond(block, instr);
        func(block, cpu, addr, instr);
        block->code.d[jmpaddr].op2 = LASTV + 1;
        return true;
    } else {
        return func(block, cpu, addr, instr);
    }
}

u32 compile_shifter(IRBlock* block, ArmCore* cpu, u8 op, u32 operand, u32 shamt,
                    bool imm, u32* carry) {
    if (!imm || shamt) {
        switch (op) {
            case S_LSL:
                if (carry) {
                    if (imm) {
                        EMITVI(LSR, operand, 32 - shamt);
                    } else {
                        EMITXV(SUB, 32, shamt, 1);
                        EMITVV(LSR, operand, LASTV);
                    }
                    *carry = EMITVI(AND, LASTV, 1);
                }
                return EMITVX(LSL, operand, shamt, imm);
            case S_LSR:
                if (carry) {
                    if (imm) {
                        EMITVI(LSR, operand, shamt - 1);
                    } else {
                        EMITVI(SUB, shamt, 1);
                        EMITVV(LSR, operand, LASTV);
                    }
                    *carry = EMITVI(AND, LASTV, 1);
                }
                return EMITVX(LSR, operand, shamt, imm);
            case S_ASR:
                if (carry) {
                    if (imm) {
                        EMITVI(LSR, operand, shamt - 1);
                    } else {
                        EMITVI(SUB, shamt, 1);
                        EMITVV(ASR, operand, LASTV);
                    }
                    *carry = EMITVI(AND, LASTV, 1);
                }
                return EMITVX(ASR, operand, shamt, imm);
            case S_ROR:
                if (carry) {
                    if (imm) {
                        EMITVI(LSR, operand, shamt - 1);
                    } else {
                        EMITVI(SUB, shamt, 1);
                        EMITVV(ROR, operand, LASTV);
                    }
                    *carry = EMITVI(AND, LASTV, 1);
                }
                return EMITVX(ROR, operand, shamt, imm);
        }
    } else {
        switch (op) {
            case S_LSL:
                return operand;
            case S_LSR:
                if (carry) *carry = EMITVI(LSR, operand, 31);
                return EMIT0I(MOV, 0);
            case S_ASR:
                if (carry) *carry = EMITVI(LSR, operand, 31);
                return EMITVI(ASR, operand, 31);
            case S_ROR:
                if (carry) *carry = EMITVI(AND, operand, 1);
                EMITI0(LOAD_FLAG, CF);
                EMIT0V(SETC, LASTV);
                return EMITV0(RRC, operand);
        }
    }
    return operand;
}

DECL_ARM_COMPILE(data_proc) {
    u32 op1, op2;
    bool imm = instr.data_proc.i;

    bool usingop1 =
        instr.data_proc.opcode != A_MOV && instr.data_proc.opcode != A_MVN;

    u32 shiftc = 0;
    if (instr.data_proc.i) {
        if (cpu->cpsr.t) {
            op2 = instr.data_proc.op2;
        } else {
            op2 = instr.data_proc.op2 & 0xff;
            u32 shift_amt = instr.data_proc.op2 >> 8;
            if (shift_amt) {
                shift_amt *= 2;
                if (instr.data_proc.s)
                    shiftc = EMIT0I(MOV, (op2 >> (shift_amt - 1)) & 1);
                op2 = (op2 >> shift_amt) | (op2 << (32 - shift_amt));
            }
        }
        if (usingop1) op1 = EMIT_LOAD_REG(instr.data_proc.rn);
    } else {
        u32 rm = instr.data_proc.op2 & 0b1111;
        u32 shift = instr.data_proc.op2 >> 4;
        u32 shift_type = (shift >> 1) & 0b11;

        u32 shamt;
        bool immshift = !(shift & 1);
        if (shift & 1) {
            if (usingop1) op1 = EMIT_LOAD_REG(instr.data_proc.rn);
            op2 = EMIT_LOAD_REG(rm);

            u32 rs = shift >> 4;
            EMIT_LOAD_REG(rs);
            shamt = EMITVI(AND, LASTV, 0xff);
            if (instr.data_proc.s) {
                op2 = compile_shifter(block, cpu, shift_type, op2, shamt,
                                      immshift, &shiftc);
                EMITI0(LOAD_FLAG, CF);
                EMIT0V(SETC, LASTV);
                shiftc = EMITVV(GETCIFZ, shamt, shiftc);
            } else {
                op2 = compile_shifter(block, cpu, shift_type, op2, shamt,
                                      immshift, NULL);
            }
        } else {
            if (usingop1) op1 = EMIT_LOAD_REG(instr.data_proc.rn);
            op2 = EMIT_LOAD_REG(rm);

            shamt = shift >> 3;
            op2 = compile_shifter(block, cpu, shift_type, op2, shamt, immshift,
                                  instr.data_proc.s ? &shiftc : NULL);
        }
    }
    if (usingop1 && instr.data_proc.rn == 15 && instr.data_proc.rd != 15 && cpu->cpsr.t) {
        op1 = EMITVI(AND, op1, ~3);
    }

    bool save = true;
    bool arith = false;
    switch (instr.data_proc.opcode) {
        case A_AND:
            EMITVX(AND, op1, op2, imm);
            break;
        case A_EOR:
            EMITVX(XOR, op1, op2, imm);
            break;
        case A_SUB:
            EMITVX(SUB, op1, op2, imm);
            arith = true;
            break;
        case A_RSB:
            EMITXV(SUB, op2, op1, imm);
            arith = true;
            break;
        case A_ADD:
            EMITVX(ADD, op1, op2, imm);
            arith = true;
            break;
        case A_ADC:
            EMITI0(LOAD_FLAG, CF);
            EMIT0V(SETC, LASTV);
            EMITVX(ADC, op1, op2, imm);
            arith = true;
            break;
        case A_SBC:
            EMITI0(LOAD_FLAG, CF);
            EMIT0V(SETC, LASTV);
            EMITVX(SBC, op1, op2, imm);
            arith = true;
            break;
        case A_RSC:
            EMITI0(LOAD_FLAG, CF);
            EMIT0V(SETC, LASTV);
            EMITXV(SBC, op2, op1, imm);
            arith = true;
            break;
        case A_TST:
            EMITVX(AND, op1, op2, imm);
            save = false;
            break;
        case A_TEQ:
            EMITVX(XOR, op1, op2, imm);
            save = false;
            break;
        case A_CMP:
            EMITVX(SUB, op1, op2, imm);
            arith = true;
            save = false;
            break;
        case A_CMN:
            EMITVX(ADD, op1, op2, imm);
            arith = true;
            save = false;
            break;
        case A_ORR:
            EMITVX(OR, op1, op2, imm);
            break;
        case A_MOV:
            EMIT0X(MOV, op2, imm);
            break;
        case A_BIC:
            EMIT0X(NOT, op2, imm);
            EMITVV(AND, op1, LASTV);
            break;
        case A_MVN:
            EMIT0X(NOT, op2, imm);
            break;
    }
    u32 vres = LASTV;

    if (instr.data_proc.s) {
        if (instr.data_proc.rd == 15) {
            if (!(cpu->cpsr.m == M_USER || cpu->cpsr.m == M_SYSTEM)) {
                EMIT00(LOAD_SPSR);
                EMIT0V(STORE_CPSR, LASTV);
                EMITI0(MODESWITCH, cpu->cpsr.m);
                if (!save) return false;
            }
        } else {
            if (arith) {
                EMIT0V(GETC, vres);
                EMITIV(STORE_FLAG, CF, LASTV);
                EMIT0V(GETV, vres);
                EMITIV(STORE_FLAG, VF, LASTV);
            } else if (shiftc) {
                EMITIV(STORE_FLAG, CF, shiftc);
            }
            EMIT0V(GETN, vres);
            EMITIV(STORE_FLAG, NF, LASTV);
            EMIT0V(GETZ, vres);
            EMITIV(STORE_FLAG, ZF, LASTV);
        }
    }
    if (save) {
        EMITV_STORE_REG(instr.data_proc.rd, vres);
        if (instr.data_proc.rd == 15) {
            if (instr.data_proc.s) {
                u32 vpc = EMITI0(LOAD_REG, 15);
                EMIT00(LOAD_THUMB);
                EMITV0(PCMASK, LASTV);
                EMITVV(AND, vpc, LASTV);
                EMITV_STORE_REG(15, LASTV);
            } else {
                EMIT_ALIGN_PC();
            }
            EMIT00(END_RET);
            return false;
        }
    }
    return true;
}

DECL_ARM_COMPILE(psr_trans) {
    if (instr.psr_trans.op) {
        u32 op2;
        bool imm = instr.psr_trans.i;
        if (instr.psr_trans.i) {
            op2 = instr.psr_trans.op2 & 0xff;
            u32 rot = instr.psr_trans.op2 >> 8 << 1;
            op2 = (op2 >> rot) | (op2 << (32 - rot));
        } else {
            u32 rm = instr.psr_trans.op2 & 0b1111;
            op2 = EMIT_LOAD_REG(rm);
        }
        u32 mask = 0;
        if (instr.psr_trans.f) mask |= 0xff000000;
        if (instr.psr_trans.s) mask |= 0x00ff0000;
        if (instr.psr_trans.x) mask |= 0x0000ff00;
        if (instr.psr_trans.c) mask |= 0x000000ff;
        if (cpu->cpsr.m == M_USER) mask &= 0xffff0000;

        if (imm) op2 &= mask;
        else op2 = EMITVI(AND, op2, mask);

        if (instr.psr_trans.p) {
            EMIT00(LOAD_SPSR);
        } else {
            EMIT00(LOAD_CPSR);
        }
        EMITVI(AND, LASTV, ~mask);
        EMITVX(OR, LASTV, op2, imm);
        if (instr.psr_trans.p) {
            EMIT0V(STORE_SPSR, LASTV);
            return true;
        } else {
            EMIT0V(STORE_CPSR, LASTV);
            if (mask & 0xffff) {
                EMITI0(MODESWITCH, cpu->cpsr.m);
                EMITI_STORE_REG(15, addr + INSTRLEN);
                EMIT00(END_RET);
                return false;
            } else {
                return true;
            }
        }
    } else {
        if (instr.psr_trans.p) {
            EMIT00(LOAD_SPSR);
        } else {
            EMIT00(LOAD_CPSR);
        }
        EMITV_STORE_REG(instr.psr_trans.rd, LASTV);
        return true;
    }
}

DECL_ARM_COMPILE(multiply) {
    u32 op1 = EMIT_LOAD_REG(instr.multiply.rm);
    u32 op2 = EMIT_LOAD_REG(instr.multiply.rs);
    u32 vres = EMITVV(MUL, op1, op2);
    if (instr.multiply.a) {
        EMIT_LOAD_REG(instr.multiply.rn);
        vres = EMITVV(ADD, vres, LASTV);
    }
    if (instr.multiply.s) {
        EMIT0V(GETN, vres);
        EMITIV(STORE_FLAG, NF, LASTV);
        EMIT0V(GETZ, vres);
        EMITIV(STORE_FLAG, ZF, LASTV);
    }
    EMITV_STORE_REG(instr.multiply.rd, vres);
    return true;
}

DECL_ARM_COMPILE(multiply_long) {
    u32 op1 = EMIT_LOAD_REG(instr.multiply_long.rm);
    u32 op2 = EMIT_LOAD_REG(instr.multiply_long.rs);
    u32 vreslo = EMITVV(MUL, op1, op2);
    u32 vreshi;
    if (instr.multiply_long.u) {
        vreshi = EMITVV(SMULH, op1, op2);
    } else {
        vreshi = EMITVV(UMULH, op1, op2);
    }
    if (instr.multiply_long.a) {
        u32 aclo = EMIT_LOAD_REG(instr.multiply_long.rdlo);
        u32 achi = EMIT_LOAD_REG(instr.multiply_long.rdhi);

        if (instr.multiply_long.aa) {
            vreslo = EMITVV(ADD, vreslo, aclo);
            vreshi = EMITVV(ADC, vreshi, achi);
        } else {
            vreslo = EMITVV(ADD, vreslo, aclo);
            vreshi = EMITVI(ADC, vreshi, 0);
            vreslo = EMITVV(ADD, vreslo, achi);
            vreshi = EMITVI(ADC, vreshi, 0);
        }
    }
    if (instr.multiply_long.s) {
        u32 zlo = EMIT0V(GETZ, vreslo);
        EMIT0V(GETZ, vreshi);
        EMITVV(AND, zlo, LASTV);
        EMITIV(STORE_FLAG, ZF, LASTV);
        EMIT0V(GETN, vreshi);
        EMITIV(STORE_FLAG, NF, LASTV);
    }
    EMITV_STORE_REG(instr.multiply_long.rdlo, vreslo);
    EMITV_STORE_REG(instr.multiply_long.rdhi, vreshi);
    return true;
}

DECL_ARM_COMPILE(multiply_short) {

    u32 vop1 = EMIT_LOAD_REG(instr.multiply_short.rs);
    if (!instr.multiply_short.y) vop1 = EMITVI(LSL, vop1, 16);
    vop1 = EMITVI(ASR, vop1, 16);

    u32 vop2 = EMIT_LOAD_REG(instr.multiply_short.rm);
    if (instr.multiply_short.op != 0b01) {
        if (!instr.multiply_short.x) vop2 = EMITVI(LSL, vop2, 16);
        vop2 = EMITVI(ASR, vop2, 16);
    }
    // s64 res = op1 * op2;
    switch (instr.multiply_short.op) {
        case 0: {
            u32 vres = EMITVV(MUL, vop1, vop2);
            EMIT_LOAD_REG(instr.multiply_short.rn);
            vres = EMITVV(ADD, vres, LASTV);
            // if (res > INT32_MAX || res < INT32_MIN) {
            //     cpu->cpsr.q = 1;
            // }
            EMITV_STORE_REG(instr.multiply_short.rd, vres);
            break;
        }
        case 1: {
            u32 vres = EMITVV(SMULW, vop1, vop2);
            if (!instr.multiply_short.x) {
                EMIT_LOAD_REG(instr.multiply_short.rn);
                vres = EMITVV(ADD, vres, LASTV);
                // if (res > INT32_MAX || res < INT32_MIN) {
                //     cpu->cpsr.q = 1;
                // }
            }
            EMITV_STORE_REG(instr.multiply_short.rd, vres);
            break;
        }
        case 2: {
            u32 vres = EMITVV(MUL, vop1, vop2);
            u32 vsx = EMITVI(ASR, vres, 31);
            u32 vrn = EMIT_LOAD_REG(instr.multiply_short.rn);
            u32 vrd = EMIT_LOAD_REG(instr.multiply_short.rd);
            u32 vareslo = EMITVV(ADD, vrn, vres);
            u32 vareshi = EMITVV(ADC, vrd, vsx);
            EMITV_STORE_REG(instr.multiply_short.rn, vareslo);
            EMITV_STORE_REG(instr.multiply_short.rd, vareshi);
            break;
        }
        case 3:
            EMITVV(MUL, vop1, vop2);
            EMITV_STORE_REG(instr.multiply_short.rd, LASTV);
            break;
    }
    return true;
}

DECL_ARM_COMPILE(swap) {
    u32 vaddr = EMIT_LOAD_REG(instr.swap.rn);
    if (instr.swap.b) {
        u32 vdata = EMITV0(LOAD_MEM8, vaddr);
        EMIT_LOAD_REG(instr.swap.rm);
        EMITVV(STORE_MEM8, vaddr, LASTV);
        EMITV_STORE_REG(instr.swap.rd, vdata);
    } else {
        u32 vdata = EMITV0(LOAD_MEM32, vaddr);
        EMIT_LOAD_REG(instr.swap.rm);
        EMITVV(STORE_MEM32, vaddr, LASTV);
        EMITV_STORE_REG(instr.swap.rd, vdata);
    }
    return true;
}

DECL_ARM_COMPILE(branch_exch) {
    u32 vdest = EMIT_LOAD_REG(instr.branch_exch.rn);
    if (instr.branch_exch.l) {
        if (cpu->cpsr.t) {
            EMITI_STORE_REG(14, addr + 3);
        } else {
            EMITI_STORE_REG(14, addr + 4);
        }
    }

    u32 vt = EMITVI(AND, vdest, 1);
    EMIT0V(STORE_THUMB, vt);
    EMITV0(PCMASK, vt);
    EMITVV(AND, vdest, LASTV);
    EMITV_STORE_REG(15, LASTV);
    EMIT00(END_RET);
    return false;
}

DECL_ARM_COMPILE(leading_zeros) {
    EMIT_LOAD_REG(instr.leading_zeros.rm);
    EMIT0V(CLZ, LASTV);
    EMITV_STORE_REG(instr.leading_zeros.rd, LASTV);
    return true;
}

DECL_ARM_COMPILE(sat_arith) {
    lwarn("unknown sat_arith");
    return true;
}

DECL_ARM_COMPILE(pack_sat) {
    if (!instr.pack_sat.p) {
        u32 shamt = instr.pack_sat.shift;
        EMIT_LOAD_REG(instr.pack_sat.rm);
        if (instr.pack_sat.x) {
            if (shamt) {
                EMITVI(ASR, LASTV, shamt);
            } else {
                EMITVI(ASR, LASTV, 31);
            }
        } else {
            if (shamt) {
                EMITVI(LSL, LASTV, shamt);
            }
        }
        u32 vop2 = LASTV;
        if (instr.pack_sat.u) {
            u32 satamt = instr.pack_sat.rn | instr.pack_sat.h << 4;
            // set q flag
            EMITIV(USAT, satamt, vop2);
            EMITV_STORE_REG(instr.pack_sat.rd, LASTV);
        } else if (instr.pack_sat.s) {
            lwarn("unknown sat %08x at %08x", instr.w, addr);
        } else {
            u32 vop1 = EMIT_LOAD_REG(instr.pack_sat.rn);
            if (instr.pack_sat.x) {
                vop2 = EMITVI(AND, vop2, MASK(16));
                vop1 = EMITVI(AND, vop1, MASK(16) << 16);
            } else {
                vop2 = EMITVI(AND, vop2, MASK(16) << 16);
                vop1 = EMITVI(AND, vop1, MASK(16));
            }
            EMITVV(OR, vop1, vop2);
            EMITV_STORE_REG(instr.pack_sat.rd, LASTV);
        }
    } else if (instr.pack_sat.x) {
        if (instr.pack_sat.s) {
            EMIT_LOAD_REG(instr.pack_sat.rm);
            if (instr.pack_sat.shift) EMITVI(ROR, LASTV, instr.pack_sat.shift);
            u32 ext = instr.pack_sat.h ? 16 : 24;
            EMITVI(LSL, LASTV, ext);
            if (instr.pack_sat.u) {
                EMITVI(LSR, LASTV, ext);
            } else {
                EMITVI(ASR, LASTV, ext);
            }
            if (instr.pack_sat.rn != 15) {
                u32 vres = LASTV;
                EMIT_LOAD_REG(instr.pack_sat.rn);
                EMITVV(ADD, vres, LASTV);
            }
            EMITV_STORE_REG(instr.pack_sat.rd, LASTV);
        } else {
            lwarn("unknown xt16 at %08x", addr);
        }
    } else {
        u32 op1 =
            instr.pack_sat.u << 2 | instr.pack_sat.s << 1 | instr.pack_sat.h;
        u32 op2 = instr.pack_sat.shift & 1;
        switch (op1) {
            case 0:
                EMITV_STORE_REG(instr.pack_sat.rd,
                                EMITVV(MEDIA_SEL,
                                       EMIT_LOAD_REG(instr.pack_sat.rn),
                                       EMIT_LOAD_REG(instr.pack_sat.rm)));
                break;
            case 3:
                if (op2) {
                    EMITV_STORE_REG(
                        instr.pack_sat.rd,
                        EMIT0V(REV16, EMIT_LOAD_REG(instr.pack_sat.rm)));
                } else {
                    EMITV_STORE_REG(
                        instr.pack_sat.rd,
                        EMIT0V(REV, EMIT_LOAD_REG(instr.pack_sat.rm)));
                }
                break;
            default:
                lwarn("unknown pack %08x at %08x", instr.w, addr);
                break;
        }
    }
    return true;
}

DECL_ARM_COMPILE(parallel_arith) {
    u32 vrn = EMIT_LOAD_REG(instr.parallel_arith.rn);
    u32 vrm = EMIT_LOAD_REG(instr.parallel_arith.rm);

    if (instr.parallel_arith.u) {
        switch (instr.parallel_arith.op1) {
            case 1:
                if (instr.parallel_arith.b) {
                    switch (instr.parallel_arith.op2) {
                        case 0:
                            EMITVV(MEDIA_UADD8, vrn, vrm);
                            EMITV_STORE_REG(instr.parallel_arith.rd, LASTV);
                            break;
                        default:
                            lwarn("unknown parallel arith uxxx8 %08x", instr.w);
                            break;
                    }
                } else {
                    lwarn("unknown parallel arith uxxx16 %08x", instr.w);
                }
                break;
            case 2:
                if (instr.parallel_arith.b) {
                    switch (instr.parallel_arith.op2) {
                        case 3:
                            EMITVV(MEDIA_UQSUB8, vrn, vrm);
                            EMITV_STORE_REG(instr.parallel_arith.rd, LASTV);
                            break;
                        default:
                            lwarn("unknown parallel arith uqxxx8 %08x",
                                  instr.w);
                            break;
                    }
                } else {
                    lwarn("unknown parallel arith uqxxx16 %08x", instr.w);
                }
                break;
            case 3:
                lwarn("unknown parallel arith uhxxxx %08x", instr.w);
                break;
        }
    } else {
        lwarn("unknown parallel arith sxxxxx %08x", instr.w);
    }
    return true;
}

DECL_ARM_COMPILE(half_trans) {
    u32 vaddr = EMIT_LOAD_REG(instr.half_trans.rn);
    u32 voffset;
    u32 immoffset = instr.half_trans.i;
    if (instr.half_trans.i) {
        voffset = instr.half_trans.offlo | (instr.half_trans.offhi << 4);
    } else {
        voffset = EMIT_LOAD_REG(instr.half_trans.offlo);
    }

    if (instr.half_trans.u) {
        EMITVX(ADD, vaddr, voffset, immoffset);
    } else {
        EMITVX(SUB, vaddr, voffset, immoffset);
    }
    u32 vwback = LASTV;
    if (instr.half_trans.p && (instr.half_trans.h || instr.half_trans.s))
        vaddr = vwback;

    if (instr.half_trans.s) {
        if (instr.half_trans.l) {
            if (instr.half_trans.w || !instr.half_trans.p) {
                EMITV_STORE_REG(instr.half_trans.rn, vwback);
            }
            if (instr.half_trans.h) {
                EMITV0(LOAD_MEMS16, vaddr);
            } else {
                EMITV0(LOAD_MEMS8, vaddr);
            }
            EMITV_STORE_REG(instr.half_trans.rd, LASTV);
            if (instr.half_trans.rd == 15) {
                EMIT_ALIGN_PC();
                EMIT00(END_RET);
                return false;
            }
            return true;
        } else {
            vaddr = EMITVI(AND, vaddr, ~3);
            if (instr.half_trans.h) {
                EMIT_LOAD_REG(instr.half_trans.rd);
                EMITVV(STORE_MEM32, vaddr, LASTV);
                vaddr = EMITVI(ADD, vaddr, 4);
                EMIT_LOAD_REG(instr.half_trans.rd + 1);
                EMITVV(STORE_MEM32, vaddr, LASTV);
                if (instr.half_trans.w || !instr.half_trans.p) {
                    EMITV_STORE_REG(instr.half_trans.rn, vwback);
                }
            } else {
                if (instr.half_trans.w || !instr.half_trans.p) {
                    EMITV_STORE_REG(instr.half_trans.rn, vwback);
                }
                EMITV0(LOAD_MEM32, vaddr);
                EMITV_STORE_REG(instr.half_trans.rd, LASTV);
                vaddr = EMITVI(ADD, vaddr, 4);
                EMITV0(LOAD_MEM32, vaddr);
                EMITV_STORE_REG(instr.half_trans.rd + 1, LASTV);
            }
            return true;
        }
    } else if (instr.half_trans.h) {
        if (instr.half_trans.l) {
            if (instr.half_trans.w || !instr.half_trans.p) {
                EMITV_STORE_REG(instr.half_trans.rn, vwback);
            }
            EMITV0(LOAD_MEM16, vaddr);
            EMITV_STORE_REG(instr.half_trans.rd, LASTV);
            if (instr.half_trans.rd == 15) {
                EMIT_ALIGN_PC();
                EMIT00(END_RET);
                return false;
            }
            return true;
        } else {
            EMIT_LOAD_REG(instr.half_trans.rd);
            EMITVV(STORE_MEM16, vaddr, LASTV);
            if (instr.half_trans.w || !instr.half_trans.p) {
                EMITV_STORE_REG(instr.half_trans.rn, vwback);
            }
            return true;
        }
    } else {
        u32 op = instr.half_trans.i << 1 | instr.half_trans.w;
        switch (op) {
            case 0:
                if (instr.half_trans.l) {
                    EMITV0(LOAD_MEM32, vaddr);
                    EMITV_STORE_REG(instr.half_trans.rd, LASTV);
                } else {
                    EMIT_LOAD_REG(instr.half_trans.offlo);
                    EMITVV(STORE_MEM32, vaddr, LASTV);
                    EMITI_STORE_REG(instr.half_trans.rd, 0);
                }
                return true;
            case 1:
                if (instr.half_trans.l) {
                    EMITV0(LOAD_MEM32, vaddr);
                    EMITV_STORE_REG(instr.half_trans.rd, LASTV);
                    vaddr = EMITVI(ADD, vaddr, 4);
                    EMITV0(LOAD_MEM32, vaddr);
                    EMITV_STORE_REG(instr.half_trans.rd + 1, LASTV);
                } else {
                    EMIT_LOAD_REG(instr.half_trans.offlo);
                    EMITVV(STORE_MEM32, vaddr, LASTV);
                    vaddr = EMITVI(ADD, vaddr, 4);
                    EMIT_LOAD_REG(instr.half_trans.offlo + 1);
                    EMITVV(STORE_MEM32, vaddr, LASTV);
                    EMITI_STORE_REG(instr.half_trans.rd, 0);
                }
                return true;
            case 2:
                if (instr.half_trans.l) {
                    EMITV0(LOAD_MEM8, vaddr);
                    EMITV_STORE_REG(instr.half_trans.rd, LASTV);
                } else {
                    EMIT_LOAD_REG(instr.half_trans.offlo);
                    EMITVV(STORE_MEM8, vaddr, LASTV);
                    EMITI_STORE_REG(instr.half_trans.rd, 0);
                }
                return true;
            case 3:
                if (instr.half_trans.l) {
                    EMITV0(LOAD_MEM16, vaddr);
                    EMITV_STORE_REG(instr.half_trans.rd, LASTV);
                } else {
                    EMIT_LOAD_REG(instr.half_trans.offlo);
                    EMITVV(STORE_MEM16, vaddr, LASTV);
                    EMITI_STORE_REG(instr.half_trans.rd, 0);
                }
                return true;
            default:
                return true;
        }
    }
}

DECL_ARM_COMPILE(single_trans) {
    if (instr.cond == 0xf) {
        return true;
    }

    u32 vaddr = EMIT_LOAD_REG(instr.single_trans.rn);
    if (instr.single_trans.rn == 15 && cpu->cpsr.t) {
        vaddr = EMITVI(AND, vaddr, ~3);
    }
    u32 voffset;
    u32 immoffset = !instr.single_trans.i;
    if (instr.single_trans.i) {
        u32 rm = instr.single_trans.offset & 0b1111;
        voffset = EMIT_LOAD_REG(rm);
        u8 shift = instr.single_trans.offset >> 4;
        u8 shamt = shift >> 3;
        u8 op = (shift >> 1) & 3;
        voffset = compile_shifter(block, cpu, op, voffset, shamt, true, NULL);
    } else {
        voffset = instr.single_trans.offset;
    }

    if (instr.single_trans.u) {
        EMITVX(ADD, vaddr, voffset, immoffset);
    } else {
        EMITVX(SUB, vaddr, voffset, immoffset);
    }
    u32 vwback = LASTV;
    if (instr.single_trans.p) vaddr = vwback;

    if (instr.single_trans.b) {
        if (instr.single_trans.l) {
            if (instr.single_trans.w || !instr.single_trans.p) {
                EMITV_STORE_REG(instr.single_trans.rn, vwback);
            }
            EMITV0(LOAD_MEM8, vaddr);
            EMITV_STORE_REG(instr.single_trans.rd, LASTV);
            if (instr.single_trans.rd == 15) {
                EMIT_ALIGN_PC();
                EMIT00(END_RET);
                return false;
            }
            return true;
        } else {
            EMIT_LOAD_REG(instr.single_trans.rd);
            EMITVV(STORE_MEM8, vaddr, LASTV);
            if (instr.single_trans.w || !instr.single_trans.p) {
                EMITV_STORE_REG(instr.single_trans.rn, vwback);
            }
            return true;
        }
    } else {
        if (instr.single_trans.l) {
            if (instr.single_trans.w || !instr.single_trans.p) {
                EMITV_STORE_REG(instr.single_trans.rn, vwback);
            }
            EMITV0(LOAD_MEM32, vaddr);
            EMITV_STORE_REG(instr.single_trans.rd, LASTV);
            if (instr.single_trans.rd == 15) {
                u32 vpc = EMITI0(LOAD_REG, 15);
                u32 vt = EMITVI(AND, vpc, 1);
                EMIT0V(STORE_THUMB, vt);
                EMITV0(PCMASK, vt);
                EMITVV(AND, vpc, LASTV);
                EMITV_STORE_REG(15, LASTV);
                EMIT00(END_RET);
                return false;
            }
            return true;
        } else {
            EMIT_LOAD_REG(instr.single_trans.rd);
            EMITVV(STORE_MEM32, vaddr, LASTV);
            if (instr.single_trans.w || !instr.single_trans.p) {
                EMITV_STORE_REG(instr.single_trans.rn, vwback);
            }
            return true;
        }
    }
}

bool needs_user_reg_op(u32 mode, int reg) {
    if (mode == M_USER || mode == M_SYSTEM || reg < 8 || reg == 15)
        return false;
    if (reg < 13) {
        return mode == M_FIQ;
    }
    return true;
}

DECL_ARM_COMPILE(block_trans) {
    int rcount = 0;
    int rlist[16];
    u32 vaddr = EMIT_LOAD_REG(instr.block_trans.rn);
    u32 wboff;
    if (instr.block_trans.rlist) {
        for (int i = 0; i < 16; i++) {
            if (instr.block_trans.rlist & BIT(i)) rlist[rcount++] = i;
        }
        wboff = rcount << 2;
    } else {
        rcount = 0;
        wboff = 64;
    }

    u32 vwback;
    if (instr.block_trans.u) {
        vwback = EMITVI(ADD, vaddr, wboff);
    } else {
        vwback = EMITVI(SUB, vaddr, wboff);
        vaddr = vwback;
    }
    if (instr.block_trans.p == instr.block_trans.u) {
        vaddr = EMITVI(ADD, vaddr, 4);
    }

    vaddr = EMITVI(AND, vaddr, ~3);

    if (instr.block_trans.s &&
        !((instr.block_trans.rlist & BIT(15)) && instr.block_trans.l)) {
        if (instr.block_trans.l) {
            for (int i = 0; i < rcount; i++) {
                if (i == rcount - 1 && instr.block_trans.w)
                    EMITV_STORE_REG(instr.block_trans.rn, vwback);
                EMITV0(LOAD_MEM32, vaddr);
                if (needs_user_reg_op(cpu->cpsr.m, rlist[i]))
                    EMITIV(STORE_REG_USR, rlist[i], LASTV);
                else EMITV_STORE_REG(rlist[i], LASTV);
                vaddr = EMITVI(ADD, vaddr, 4);
            }
            if (rcount < 2 && instr.block_trans.w)
                EMITV_STORE_REG(instr.block_trans.rn, vwback);
        } else {
            for (int i = 0; i < rcount; i++) {
                if (needs_user_reg_op(cpu->cpsr.m, rlist[i]))
                    EMITI0(LOAD_REG_USR, rlist[i]);
                else EMIT_LOAD_REG(rlist[i]);
                EMITVV(STORE_MEM32, vaddr, LASTV);
                vaddr = EMITVI(ADD, vaddr, 4);
            }
            if (instr.block_trans.w)
                EMITV_STORE_REG(instr.block_trans.rn, vwback);
        }
        return true;
    } else {
        if (instr.block_trans.l) {
            if (cpu->cpsr.t) {
                if (instr.block_trans.w)
                    EMITV_STORE_REG(instr.block_trans.rn, vwback);
                for (int i = 0; i < rcount; i++) {
                    EMITV0(LOAD_MEM32, vaddr);
                    EMITV_STORE_REG(rlist[i], LASTV);
                    vaddr = EMITVI(ADD, vaddr, 4);
                }
            } else {
                for (int i = 0; i < rcount; i++) {
                    if (i == rcount - 1 && instr.block_trans.w)
                        EMITV_STORE_REG(instr.block_trans.rn, vwback);
                    EMITV0(LOAD_MEM32, vaddr);
                    EMITV_STORE_REG(rlist[i], LASTV);
                    vaddr = EMITVI(ADD, vaddr, 4);
                }
                if (rcount < 2 && instr.block_trans.w)
                    EMITV_STORE_REG(instr.block_trans.rn, vwback);
            }
            if (instr.block_trans.rlist & BIT(15)) {
                if (instr.block_trans.s) {
                    if (!(cpu->cpsr.m == M_USER || cpu->cpsr.m == M_SYSTEM)) {
                        EMIT00(LOAD_SPSR);
                        EMIT0V(STORE_CPSR, LASTV);
                        EMITI0(MODESWITCH, cpu->cpsr.m);
                    }
                }
                u32 vpc = EMITI0(LOAD_REG, 15);
                u32 vt = EMITVI(AND, vpc, 1);
                EMIT0V(STORE_THUMB, vt);
                EMITV0(PCMASK, vt);
                EMITVV(AND, vpc, LASTV);
                EMITV_STORE_REG(15, LASTV);
                EMIT00(END_RET);
                return false;
            }
            return true;
        } else {
            for (int i = 0; i < rcount; i++) {
                EMIT_LOAD_REG(rlist[i]);
                EMITVV(STORE_MEM32, vaddr, LASTV);
                vaddr = EMITVI(ADD, vaddr, 4);
            }
            if (instr.block_trans.w)
                EMITV_STORE_REG(instr.block_trans.rn, vwback);
            return true;
        }
    }
}

DECL_ARM_COMPILE(branch) {
    u32 offset = instr.branch.offset;
    offset = (s32) (offset << 8) >> 8;
    if (cpu->cpsr.t) offset <<= 1;
    else offset <<= 2;
    u32 dest = addr + 2 * INSTRLEN + offset;
    if (instr.branch.l || instr.cond == 0xf) {
        if (cpu->cpsr.t) {
            if (offset & BIT(23)) {
                offset %= BIT(23);
                EMIT_LOAD_REG(14);
                u32 vdest = EMITVI(ADD, LASTV, offset);
                EMITI_STORE_REG(14, addr + 3);
                if (instr.cond == 0xf) {
                    EMIT0I(STORE_THUMB, 0);
                    vdest = EMITVI(AND, vdest, ~3);
                }
                EMITV_STORE_REG(15, vdest);
                EMIT00(END_RET);
                return false;
            } else {
                if (offset & BIT(22)) dest += 0xff800000;
                EMITI_STORE_REG(14, dest);
                return true;
            }
        } else {
            EMITI_STORE_REG(14, addr + 4);
            if (instr.cond == 0xf) {
                dest += instr.branch.l << 1;
                EMIT0I(STORE_THUMB, 1);
            }
        }
    }

    EMITI_STORE_REG(15, dest);
    if (dest == block->start_addr) {
        block->loop = true;
        EMIT00(NOP);
    }
    EMIT00(END_RET);
    return false;
}

DECL_ARM_COMPILE(cp_double_reg_trans) {
    if ((instr.cp_double_reg_trans.cpnum & ~1) == 10) {
        if (instr.cp_double_reg_trans.l) {
            u32 vlo = EMITI0(VFP_READ64L, instr.w);
            u32 vhi = EMITI0(VFP_READ64H, instr.w);
            EMITV_STORE_REG(instr.cp_double_reg_trans.rdlo, vlo);
            EMITV_STORE_REG(instr.cp_double_reg_trans.rdhi, vhi);
        } else {
            u32 vlo = EMIT_LOAD_REG(instr.cp_double_reg_trans.rdlo);
            u32 vhi = EMIT_LOAD_REG(instr.cp_double_reg_trans.rdhi);
            EMITIV(VFP_WRITE64L, instr.w, vlo);
            EMITIV(VFP_WRITE64H, instr.w, vhi);
        }
    } else {
        lerror("unknown coprocessor cp%d", instr.cp_reg_trans.cpnum);
    }
    return true;
}

DECL_ARM_COMPILE(cp_data_trans) {
    if ((instr.cp_data_trans.cpnum & ~1) == 10) {
        u32 vaddr = EMIT_LOAD_REG(instr.cp_data_trans.rn);
        u32 offset = instr.cp_data_trans.offset << 2;

        if (instr.cp_data_trans.u) {
            EMITVI(ADD, vaddr, offset);
        } else {
            EMITVI(SUB, vaddr, offset);
        }
        u32 vwback = LASTV;
        if (instr.cp_data_trans.p) vaddr = vwback;

        if (instr.cp_data_trans.w) {
            EMITV_STORE_REG(instr.cp_data_trans.rn, vwback);
        }

        if (instr.cp_data_trans.l) {
            EMITIV(VFP_LOAD_MEM, instr.w, vaddr);
        } else {
            EMITIV(VFP_STORE_MEM, instr.w, vaddr);
        }
    } else {
        lerror("unknown coprocessor cp%d", instr.cp_reg_trans.cpnum);
    }
    return true;
}

DECL_ARM_COMPILE(cp_data_proc) {
    if ((instr.cp_data_proc.cpnum & ~1) == 10) {
        EMITI0(VFP_DATA_PROC, instr.w);
    } else {
        lerror("unknown coprocessor cp%d", instr.cp_reg_trans.cpnum);
    }
    return true;
}

DECL_ARM_COMPILE(cp_reg_trans) {
    if ((instr.cp_reg_trans.cpnum & ~1) == 10) {
        if (instr.cp_reg_trans.l) {
            EMITI0(VFP_READ, instr.w);
            if (instr.cp_reg_trans.rd == 15) {
                u32 tmp = EMITVI(AND, LASTV, 0xf0000000);
                EMIT00(LOAD_CPSR);
                EMITVI(AND, LASTV, 0x0fffffff);
                EMITVV(OR, LASTV, tmp);
                EMIT0V(STORE_CPSR, LASTV);
            } else {
                EMITV_STORE_REG(instr.cp_reg_trans.rd, LASTV);
            }
        } else {
            EMIT_LOAD_REG(instr.cp_reg_trans.rd);
            EMITIV(VFP_WRITE, instr.w, LASTV);
        }
    } else if (instr.cp_reg_trans.cpnum == 15 &&
               instr.cp_reg_trans.cpopc == 0) {
        if (instr.cp_reg_trans.l) {
            EMITI0(CP15_READ, instr.w);
            EMITV_STORE_REG(instr.cp_reg_trans.rd, LASTV);
        } else {
            EMIT_LOAD_REG(instr.cp_reg_trans.rd);
            EMITIV(CP15_WRITE, instr.w, LASTV);
            return false;
        }
    } else {
        lerror("unknown coprocessor cp%d", instr.cp_reg_trans.cpnum);
    }
    return true;
}

DECL_ARM_COMPILE(undefined) {
    lerror("undefined instruction %08x at %08x", instr.w, addr);
    return true;
}

DECL_ARM_COMPILE(sw_intr) {
    EMITI_STORE_REG(15, addr + INSTRLEN);
    EMITII(EXCEPTION, E_SWI, instr.w);
    EMIT00(END_RET);
    return false;
}