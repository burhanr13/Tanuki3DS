#ifdef __aarch64__

#include "backend_arm.h"

#ifndef NOCAPSTONE
#include <capstone/capstone.h>
#endif

#include "arm/media.h"

#define RAS_DEFAULT_SUFFIX W
#define RAS_CTX_VAR backend->code
#include <ras/ras_a64.h>

#define TEMPREGS_BASE 3
#define TEMPREGS_COUNT 13
#define SAVEDREGS_BASE 19
#define SAVEDREGS_COUNT 10

// returns:
// 0-31 : reg index
// 32+n : stack index
static int getOpForReg(HostRegAllocation* hralloc, int i) {
    HostRegInfo hr = hralloc->hostreg_info[i];
    switch (hr.type) {
        case REG_TEMP:
            return TEMPREGS_BASE + hr.index;
        case REG_SAVED:
            return SAVEDREGS_BASE + hr.index;
        case REG_STACK:
            return 32 + hr.index;
        default:
            unreachable();
    }
}

[[maybe_unused]]
static void print_hostregs(HostRegAllocation* hralloc) {
    printf("Host Regs:");
    for (u32 i = 0; i < hralloc->nregs; i++) {
        printf(" $%d:", i);
        int operand = getOpForReg(hralloc, i);
        if (operand >= 32) {
            operand -= 32;
            printf("[sp, #0x%x]", 4 * operand);
        } else {
            printf("w%d", operand);
        }
    }
    printf("\n");
}

#define SPDISP() ((backend->hralloc.count[REG_STACK] * 4 + 15) & ~15)

static int getOp(ArmCodeBackend* backend, int i) {
    int assn = backend->regalloc->reg_assn[i];
    if (assn == -1) return -1;
    else return getOpForReg(&backend->hralloc, assn);
}

static void compileVFPDataProc(ArmCodeBackend* backend, ArmInstr instr);
static void compileVFPLoadMem(ArmCodeBackend* backend, ArmInstr instr,
                              rasA64Reg addr, rasLabel lldf32, rasLabel lldf64);
static void compileVFPStoreMem(ArmCodeBackend* backend, ArmInstr instr,
                               rasA64Reg addr, rasLabel lstf32, rasLabel lstf64);
static void compileVFPRead(ArmCodeBackend* backend, ArmInstr instr, rasA64Reg dst);
static void compileVFPWrite(ArmCodeBackend* backend, ArmInstr instr,
                            rasA64Reg src);
static void compileVFPRead64(ArmCodeBackend* backend, ArmInstr instr,
                             rasA64Reg dst, bool h);
static void compileVFPWrite64(ArmCodeBackend* backend, ArmInstr instr,
                              rasA64Reg src, bool h);

#define GETOP(i) getOp(backend, i)

#define CPU(m, ...) (R29, offsetof(ArmCore, m) __VA_OPT__(+) __VA_ARGS__)

#define LOADOP(i, flbk)                                                        \
    ({                                                                         \
        auto dst = flbk;                                                       \
        if (inst.imm##i) {                                                     \
            MOV(dst, inst.op##i);                                              \
        } else {                                                               \
            int op = GETOP(inst.op##i);                                        \
            if (op >= 32) {                                                    \
                op -= 32;                                                      \
                LDR(dst, (SP, 4 * op));                                        \
            } else {                                                           \
                dst = R(op);                                                 \
            }                                                                  \
        }                                                                      \
        dst;                                                                   \
    })
#define LOADOP1() LOADOP(1, IP0)
#define LOADOP2() LOADOP(2, IP1)

#define MOVOP(i, dst)                                                          \
    ({                                                                         \
        auto src = LOADOP(i, dst);                                             \
        if (src.idx != dst.idx) MOV(dst, src);                                 \
    })
#define MOVOP1(dst) MOVOP(1, dst)
#define MOVOP2(dst) MOVOP(2, dst)

#define DSTREG()                                                               \
    ({                                                                         \
        int op = GETOP(i);                                                     \
        (op >= 32) ? IP0 : R(op);                                            \
    })
#define STOREDST()                                                             \
    ({                                                                         \
        int op = GETOP(i);                                                     \
        if (op >= 32) {                                                        \
            op -= 32;                                                          \
            STR(IP0, (SP, 4 * op));                                            \
        }                                                                      \
    })

ArmCodeBackend* backend_arm_generate_code(IRBlock* ir, RegAllocation* regalloc,
                                          ArmCore* cpu) {
    ArmCodeBackend* backend = calloc(1, sizeof *backend);
    backend->code = rasCreate(16384);
    backend->cpu = cpu;
    backend->regalloc = regalloc;

    backend->hralloc =
        allocate_host_registers(regalloc, TEMPREGS_COUNT, SAVEDREGS_COUNT);

    u32 flags_mask = 0; // mask for which flags to store
    u32 lastflags = 0;  // last var for which flags were set

    u32 jmptarget = -1;

    // callback address literal pool
    LABEL(lld8);
    LABEL(lld16);
    LABEL(lld32);
    LABEL(lst8);
    LABEL(lst16);
    LABEL(lst32);
    LABEL(lldf32);
    LABEL(lldf64);
    LABEL(lstf32);
    LABEL(lstf64);

    // label for END_LOOP (jump to the same the block)
    LABEL(looplabel);

    // labels for jump instructions
    rasLabel labels[g_jit_config.max_block_instrs];
    int nlabel = 0;

    for (u32 i = 0; i < ir->code.size; i++) {
        while (i < ir->code.size && ir->code.d[i].opcode == IR_NOP) i++;
        if (i == ir->code.size) break;
        IRInstr inst = ir->code.d[i];
        if (i >= jmptarget && inst.opcode != IR_JELSE) {
            L(labels[nlabel - 1]);
            jmptarget = -1;
            lastflags = 0;
        }
        if (iropc_iscallback(inst.opcode)) lastflags = 0;

        switch (inst.opcode) {
            case IR_LOAD_REG: {
                auto dst = DSTREG();
                LDR(dst, CPU(r[inst.op1]));
                break;
            }
            case IR_STORE_REG: {
                auto src = LOADOP2();
                STR(src, CPU(r[inst.op1]));
                break;
            }
            case IR_LOAD_REG_USR: {
                auto dst = DSTREG();
                int rd = inst.op1;
                if (rd < 13) {
                    LDR(dst, CPU(banked_r8_12[0][rd - 8]));
                } else if (rd == 13) {
                    LDR(dst, CPU(banked_sp[0]));
                } else if (rd == 14) {
                    LDR(dst, CPU(banked_lr[0]));
                }
                break;
            }
            case IR_STORE_REG_USR: {
                auto src = LOADOP2();
                int rd = inst.op1;
                if (rd < 13) {
                    STR(src, CPU(banked_r8_12[0][rd - 8]));
                } else if (rd == 13) {
                    STR(src, CPU(banked_sp[0]));
                } else if (rd == 14) {
                    STR(src, CPU(banked_lr[0]));
                }
                break;
            }
            case IR_LOAD_FLAG: {
                auto dst = DSTREG();
                LDR(dst, CPU(cpsr));
                UBFX(dst, dst, 31 - inst.op1, 1);
                break;
            }
            case IR_STORE_FLAG: {
                if (ir->code.d[i - 2].opcode != IR_STORE_FLAG) {
                    MOV(R0, 0);
                    flags_mask = 0;
                }
                if (inst.imm2) {
                    if (inst.op2) ORR(R0, R0, BIT(31 - inst.op1));
                } else {
                    auto src = LOADOP2();
                    BFI(R0, src, 31 - inst.op1, 1);
                }
                flags_mask |= BIT(31 - inst.op1);
                if (ir->code.d[i + 2].opcode != IR_STORE_FLAG) {
                    LDR(R1, CPU(cpsr));
                    AND(R1, R1, ~flags_mask, R2);
                    ORR(R1, R1, R0);
                    STR(R1, CPU(cpsr));
                }
                break;
            }
            case IR_LOAD_CPSR: {
                auto dst = DSTREG();
                LDR(dst, CPU(cpsr));
                break;
            }
            case IR_STORE_CPSR: {
                auto src = LOADOP2();
                STR(src, CPU(cpsr));
                break;
            }
            case IR_LOAD_SPSR: {
                auto dst = DSTREG();
                LDR(dst, CPU(spsr));
                break;
            }
            case IR_STORE_SPSR: {
                auto src = LOADOP2();
                STR(src, CPU(spsr));
                break;
            }
            case IR_LOAD_THUMB: {
                auto dst = DSTREG();
                LDR(dst, CPU(cpsr));
                UBFX(dst, dst, 5, 1);
                break;
            }
            case IR_STORE_THUMB: {
                auto src = LOADOP2();
                LDR(R0, CPU(cpsr));
                BFI(R0, src, 5, 1);
                STR(R0, CPU(cpsr));
                break;
            }
            case IR_VFP_DATA_PROC: {
                lastflags = 0;
                compileVFPDataProc(backend, (ArmInstr) {inst.op1});
                break;
            }
            case IR_VFP_LOAD_MEM: {
                auto addr = LOADOP2();
                compileVFPLoadMem(backend, (ArmInstr) {inst.op1}, addr, lldf32,
                                  lldf64);
                break;
            }
            case IR_VFP_STORE_MEM: {
                auto addr = LOADOP2();
                compileVFPStoreMem(backend, (ArmInstr) {inst.op1}, addr, lstf32,
                                   lstf64);
                break;
            }
            case IR_VFP_READ: {
                auto dst = DSTREG();
                compileVFPRead(backend, (ArmInstr) {inst.op1}, dst);
                break;
            }
            case IR_VFP_WRITE: {
                auto src = LOADOP2();
                compileVFPWrite(backend, (ArmInstr) {inst.op1}, src);
                break;
            }
            case IR_VFP_READ64L: {
                auto dst = DSTREG();
                compileVFPRead64(backend, (ArmInstr) {inst.op1}, dst, false);
                break;
            }
            case IR_VFP_READ64H: {
                auto dst = DSTREG();
                compileVFPRead64(backend, (ArmInstr) {inst.op1}, dst, true);
                break;
            }
            case IR_VFP_WRITE64L: {
                auto src = LOADOP2();
                compileVFPWrite64(backend, (ArmInstr) {inst.op1}, src, false);
                break;
            }
            case IR_VFP_WRITE64H: {
                auto src = LOADOP2();
                compileVFPWrite64(backend, (ArmInstr) {inst.op1}, src, true);
                break;
            }
            case IR_CP15_READ: {
                auto dst = DSTREG();
                MOVX(R0, R29);
                MOV(R1, inst.op1);
                MOVX(IP0, (uintptr_t) cpu->cp15_read);
                BLR(IP0);
                MOV(dst, R0);
                break;
            }
            case IR_CP15_WRITE: {
                auto src = LOADOP2();
                MOVX(R0, R29);
                MOV(R1, inst.op1);
                MOV(R2, src);
                MOVX(IP0, (uintptr_t) cpu->cp15_write);
                BLR(IP0);
                break;
            }
            case IR_LOAD_MEM8: {
                auto dst = DSTREG();
                MOVX(R0, R29);
                MOVOP1(R1);
                MOV(R2, 0);
                LDRLX(IP0, lld8);
                BLR(IP0);
                MOV(dst, R0);
                break;
            }
            case IR_LOAD_MEMS8: {
                auto dst = DSTREG();
                MOVX(R0, R29);
                MOVOP1(R1);
                MOV(R2, 1);
                LDRLX(IP0, lld8);
                BLR(IP0);
                MOV(dst, R0);
                break;
            }
            case IR_LOAD_MEM16: {
                auto dst = DSTREG();
                MOVX(R0, R29);
                MOVOP1(R1);
                MOV(R2, 0);
                LDRLX(IP0, lld16);
                BLR(IP0);
                MOV(dst, R0);
                break;
            }
            case IR_LOAD_MEMS16: {
                auto dst = DSTREG();
                MOVX(R0, R29);
                MOVOP1(R1);
                MOV(R2, 1);
                LDRLX(IP0, lld16);
                BLR(IP0);
                MOV(dst, R0);
                break;
            }
            case IR_LOAD_MEM32: {
                auto dst = DSTREG();
                MOVX(R0, R29);
                MOVOP1(R1);
                LDRLX(IP0, lld32);
                BLR(IP0);
                MOV(dst, R0);
                break;
            }
            case IR_STORE_MEM8: {
                MOVX(R0, R29);
                MOVOP1(R1);
                MOVOP2(R2);
                LDRLX(IP0, lst8);
                BLR(IP0);
                break;
            }
            case IR_STORE_MEM16: {
                MOVX(R0, R29);
                MOVOP1(R1);
                MOVOP2(R2);
                LDRLX(IP0, lst16);
                BLR(IP0);
                break;
            }
            case IR_STORE_MEM32: {
                MOVX(R0, R29);
                MOVOP1(R1);
                MOVOP2(R2);
                LDRLX(IP0, lst32);
                BLR(IP0);
                break;
            }
            case IR_MOV: {
                auto src = LOADOP2();
                auto dst = DSTREG();
                MOV(dst, src);
                break;
            }
            case IR_AND: {
                if (inst.imm2) {
                    auto src1 = LOADOP1();
                    auto dst = DSTREG();
                    ANDS(dst, src1, inst.op2, IP1);
                } else if (inst.imm1) {
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    ANDS(dst, src2, inst.op1, IP0);
                } else {
                    auto src1 = LOADOP1();
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    ANDS(dst, src1, src2);
                }
                lastflags = i;
                break;
            }
            case IR_OR: {
                if (inst.imm2) {
                    auto src1 = LOADOP1();
                    auto dst = DSTREG();
                    ORR(dst, src1, inst.op2, IP1);
                } else if (inst.imm1) {
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    ORR(dst, src2, inst.op1, IP0);
                } else {
                    auto src1 = LOADOP1();
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    ORR(dst, src1, src2);
                }
                break;
            }
            case IR_XOR: {
                if (inst.imm2) {
                    auto src1 = LOADOP1();
                    auto dst = DSTREG();
                    EOR(dst, src1, inst.op2, IP1);
                } else if (inst.imm1) {
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    EOR(dst, src2, inst.op1, IP0);
                } else {
                    auto src1 = LOADOP1();
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    EOR(dst, src1, src2);
                }
                break;
            }
            case IR_NOT: {
                auto src = LOADOP2();
                auto dst = DSTREG();
                MVN(dst, src);
                break;
            }
            case IR_LSL: {
                auto src = LOADOP1();
                auto dst = DSTREG();
                if (inst.imm2) {
                    if (inst.op2 >= 32) {
                        MOV(dst, 0);
                    } else {
                        LSL(dst, src, inst.op2);
                    }
                } else {
                    auto shift = LOADOP2();
                    CMP(shift, 32);
                    LSL(dst, src, shift);
                    CMOV(dst, ZR, HS);
                    lastflags = 0;
                }
                break;
            }
            case IR_LSR: {
                auto src = LOADOP1();
                auto dst = DSTREG();
                if (inst.imm2) {
                    if (inst.op2 >= 32) {
                        MOV(dst, 0);
                    } else {
                        LSR(dst, src, inst.op2);
                    }
                } else {
                    auto shift = LOADOP2();
                    CMP(shift, 32);
                    LSR(dst, src, shift);
                    CMOV(dst, ZR, HS);
                    lastflags = 0;
                    lastflags = 0;
                }
                break;
            }
            case IR_ASR: {
                auto src = LOADOP1();
                auto dst = DSTREG();
                if (inst.imm2) {
                    if (inst.op2 >= 32) {
                        ASR(dst, src, 31);
                    } else {
                        ASR(dst, src, inst.op2);
                    }
                } else {
                    auto shift = LOADOP2();
                    CMP(shift, 32);
                    ASR(R0, src, 31);
                    ASR(dst, src, shift);
                    CMOV(dst, R0, HS);
                    lastflags = 0;
                }
                break;
            }

            case IR_ROR: {
                auto src = LOADOP1();
                auto dst = DSTREG();
                if (inst.imm2) {
                    ROR(dst, src, inst.op2 % 32);
                } else {
                    auto shift = LOADOP2();
                    ROR(dst, src, shift);
                }
                break;
            }
            case IR_RRC: {
                auto src = LOADOP1();
                auto dst = DSTREG();
                CSET(R0, CS);
                EXTR(dst, R0, src, 1);
                break;
            }
            case IR_ADD: {
                if (inst.imm2) {
                    auto src1 = LOADOP1();
                    auto dst = DSTREG();
                    ADDS(dst, src1, inst.op2, IP1);
                } else if (inst.imm1) {
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    ADDS(dst, src2, inst.op1, IP0);
                } else {
                    auto src1 = LOADOP1();
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    ADDS(dst, src1, src2);
                }
                lastflags = i;
                break;
            }
            case IR_SUB: {
                if (inst.imm2) {
                    auto src1 = LOADOP1();
                    auto dst = DSTREG();
                    SUBS(dst, src1, inst.op2, IP1);
                } else {
                    auto src1 = LOADOP1();
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    SUBS(dst, src1, src2);
                }
                lastflags = i;
                break;
            }
            case IR_ADC: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                ADCS(dst, src1, src2);
                lastflags = i;
                break;
            }
            case IR_SBC: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                SBCS(dst, src1, src2);
                lastflags = i;
                break;
            }
            case IR_MUL: {
                IRInstr hinst = ir->code.d[i + 1];
                if (hinst.opcode == IR_SMULH || hinst.opcode == IR_UMULH) {
                    auto src1 = LOADOP1();
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    if (hinst.opcode == IR_SMULH) {
                        SMULL(dst, src1, src2);
                    } else {
                        UMULL(dst, src1, src2);
                    }
                    STOREDST();
                    i++;
                    auto dsth = DSTREG();
                    LSRX(dsth, dst, 32);
                } else {
                    auto src1 = LOADOP1();
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    MUL(dst, src1, src2);
                }
                break;
            }
            case IR_SMULH: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                SMULL(dst, src1, src2);
                LSRX(dst, dst, 32);
                break;
            }
            case IR_UMULH: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                UMULL(dst, src1, src2);
                LSRX(dst, dst, 32);
                break;
            }
            case IR_SMULW: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                SMULL(dst, src1, src2);
                LSRX(dst, dst, 16);
                break;
            }
            case IR_CLZ: {
                auto src = LOADOP2();
                auto dst = DSTREG();
                CLZ(dst, src);
                break;
            }
            case IR_REV: {
                auto src = LOADOP2();
                auto dst = DSTREG();
                REV(dst, src);
                break;
            }
            case IR_REV16: {
                auto src = LOADOP2();
                auto dst = DSTREG();
                REV16(dst, src);
                break;
            }
            case IR_USAT: {
                auto src = LOADOP2();
                auto dst = DSTREG();
                CMP(src, 0);
                CSEL(dst, ZR, src, LT);
                MOV(R0, MASK(inst.op1));
                CMP(src, R0);
                CMOV(dst, R0, GT);
                break;
            }
            case IR_SSAT: {
                auto src = LOADOP2();
                auto dst = DSTREG();
                MOV(R0, ~MASK(inst.op1));
                CMP(src, R0);
                CSEL(dst, R0, src, LT);
                MOV(R0, MASK(inst.op1));
                CMP(src, R0);
                CMOV(dst, R0, GT);
                break;
            }
            case IR_MEDIA_UADD8: {
                auto dst = DSTREG();
                MOVX(R0, R29);
                MOVOP1(R1);
                MOVOP2(R2);
                MOVX(IP0, (uintptr_t) media_uadd8);
                BLR(IP0);
                MOV(dst, R0);
                break;
            }
            case IR_MEDIA_USUB8: {
                auto dst = DSTREG();
                MOVX(R0, R29);
                MOVOP1(R1);
                MOVOP2(R2);
                MOVX(IP0, (uintptr_t) media_usub8);
                BLR(IP0);
                MOV(dst, R0);
                break;
            }
            case IR_MEDIA_UQADD8: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                MOVS(V0, 0, src1);
                MOVS(V1, 0, src2);
                UQADD8B(V0, V0, V1);
                MOVS(dst, V0, 0);
                break;
            }
            case IR_MEDIA_UQSUB8: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                MOVS(V0, 0, src1);
                MOVS(V1, 0, src2);
                UQSUB8B(V0, V0, V1);
                MOVS(dst, V0, 0);
                break;
            }
            case IR_MEDIA_UHADD8: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                MOVS(V0, 0, src1);
                MOVS(V1, 0, src2);
                UHADD8B(V0, V0, V1);
                MOVS(dst, V0, 0);
                break;
            }
            case IR_MEDIA_SSUB8: {
                auto dst = DSTREG();
                MOVX(R0, R29);
                MOVOP1(R1);
                MOVOP2(R2);
                MOVX(IP0, (uintptr_t) media_ssub8);
                BLR(IP0);
                MOV(dst, R0);
                break;
            }
            case IR_MEDIA_QSUB8: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                MOVS(V0, 0, src1);
                MOVS(V1, 0, src2);
                SQSUB8B(V0, V0, V1);
                MOVS(dst, V0, 0);
                break;
            }
            case IR_MEDIA_SEL: {
                auto dst = DSTREG();
                MOVX(R0, R29);
                MOVOP1(R1);
                MOVOP2(R2);
                MOVX(IP0, (uintptr_t) media_sel);
                BLR(IP0);
                MOV(dst, R0);
                break;
            }
            case IR_GETN: {
                auto dst = DSTREG();
                if (inst.imm2) {
                    MOV(dst, inst.op2 >> 31);
                } else {
                    if (lastflags != inst.op2) {
                        auto src = LOADOP2();
                        TST(src, src);
                        lastflags = inst.op2;
                    }
                    CSET(dst, MI);
                }
                break;
            }
            case IR_GETZ: {
                auto dst = DSTREG();
                if (inst.imm2) {
                    MOV(dst, inst.op2 == 0);
                } else {
                    if (lastflags != inst.op2) {
                        auto src = LOADOP2();
                        TST(src, src);
                        lastflags = inst.op2;
                    }
                    CSET(dst, EQ);
                }
                break;
            }
            case IR_GETC: {
                auto dst = DSTREG();
                CSET(dst, CS);
                break;
            }
            case IR_SETC: {
                auto src = LOADOP2();
                CMP(src, 1);
                lastflags = 0;
                break;
            }
            case IR_GETCIFZ: {
                auto cond = LOADOP1();
                auto src = LOADOP2();
                auto dst = DSTREG();
                CSET(R0, CS);
                TST(cond, cond);
                CSEL(dst, R0, src, EQ);
                lastflags = 0;
                break;
            }
            case IR_GETV: {
                auto dst = DSTREG();
                CSET(dst, VS);
                break;
            }
            case IR_PCMASK: {
                auto src = LOADOP1();
                auto dst = DSTREG();
                LSL(dst, src, 1);
                SUB(dst, dst, 4);
                break;
            }
            case IR_JZ: {
                jmptarget = inst.op2;
                labels[nlabel++] = LNEW();
                auto cond = LOADOP1();
                CBZ(cond, labels[nlabel - 1]);
                break;
            }
            case IR_JNZ: {
                jmptarget = inst.op2;
                labels[nlabel++] = LNEW();
                auto cond = LOADOP1();
                CBNZ(cond, labels[nlabel - 1]);
                break;
            }
            case IR_JELSE: {
                jmptarget = inst.op2;
                labels[nlabel++] = LNEW();
                B(labels[nlabel - 1]);
                L(labels[nlabel - 2]);
                break;
            }
            case IR_MODESWITCH: {
                MOVX(R0, R29);
                MOV(R1, inst.op1);
                MOVX(IP0, (uintptr_t) cpu_update_mode);
                BLR(IP0);
                break;
            }
            case IR_EXCEPTION: {
                switch (inst.op1) {
                    case E_SWI:
                        MOVX(R0, R29);
                        MOV(R1, (ArmInstr) {inst.op2}.sw_intr.arg);
                        MOVX(IP0, (uintptr_t) cpu->handle_svc);
                        BLR(IP0);
                        break;
                    case E_UND:
                        MOVX(R0, R29);
                        MOV(R1, inst.op2);
                        MOVX(IP0, (uintptr_t) cpu_undefined_fail);
                        BLR(IP0);
                        break;
                }
                break;
            }
            case IR_HALT: {
                MOV(R0, 1);
                STRB(R0, CPU(halt));
                break;
            }
            case IR_BEGIN: {

                PUSH(FP, LR);
                for (int i = 0; i < backend->hralloc.count[REG_SAVED]; i += 2) {
                    PUSH(R(SAVEDREGS_BASE + i), R(SAVEDREGS_BASE + i + 1));
                }
                int spdisp = SPDISP();
                if (spdisp) SUBX(SP, SP, spdisp);

                MOVX(R29, (uintptr_t) cpu);
                L(looplabel);

                break;
            }
            case IR_END_RET:
            case IR_END_LINK:
            case IR_END_LOOP: {
                lastflags = 0;

                LDRX(R0, CPU(cycles));
                SUBX(R0, R0, inst.cycles);
                STRX(R0, CPU(cycles));

                if (inst.opcode == IR_END_LOOP) {
                    CMPX(R0, 0);
                    BGT(looplabel);
                }

                int spdisp = SPDISP();
                if (spdisp) ADDX(SP, SP, spdisp);
                for (int i = (backend->hralloc.count[REG_SAVED] - 1) & ~1;
                     i >= 0; i -= 2) {
                    POP(R(SAVEDREGS_BASE + i), R(SAVEDREGS_BASE + i + 1));
                }
                POP(FP, LR);

                if (inst.opcode == IR_END_LINK) {
                    LABEL(nolink);
                    LABEL(linkaddr);
                    CMPX(R0, 0);
                    BLE(nolink);
                    LDRLX(IP0, linkaddr);
                    BR(IP0);
                    ALIGN(8);
                    L(linkaddr);
                    LABEL(linklabel);
                    ArmLinkPatch p = {linklabel, inst.op1, inst.op2};
                    Vec_push(backend->links, p);
                    DWORD(linklabel);
                    L(nolink);
                }

                RET();
                break;
            }
            default:
                lerror("unimpl ir instr %d", inst.opcode);
                break;
        }
        STOREDST();
    }

    ALIGN(8);
    L(lld8);
    DWORD(LNEW(cpu->read8));
    L(lld16);
    DWORD(LNEW(cpu->read16));
    L(lld32);
    DWORD(LNEW(cpu->read32));
    L(lst8);
    DWORD(LNEW(cpu->write8));
    L(lst16);
    DWORD(LNEW(cpu->write16));
    L(lst32);
    DWORD(LNEW(cpu->write32));
    L(lldf32);
    DWORD(LNEW(cpu->readf32));
    L(lldf64);
    DWORD(LNEW(cpu->readf64));
    L(lstf32);
    DWORD(LNEW(cpu->writef32));
    L(lstf64);
    DWORD(LNEW(cpu->writef64));

    return backend;
}

static void compileVFPDataProc(ArmCodeBackend* backend, ArmInstr instr) {
    bool dp = instr.cp_data_proc.cpnum & 1;
    u32 vd = instr.cp_data_proc.crd;
    u32 vn = instr.cp_data_proc.crn;
    u32 vm = instr.cp_data_proc.crm;
    if (!dp) {
        vd = vd << 1 | ((instr.cp_data_proc.cpopc >> 2) & 1);
        vn = vn << 1 | (instr.cp_data_proc.cp >> 2);
        vm = vm << 1 | (instr.cp_data_proc.cp & 1);
    }

    u32 cpopc = instr.cp_data_proc.cpopc & 0b1011;
    bool op = instr.cp_data_proc.cp & 2;

    switch (cpopc) {
        case 0:
        case 1:
            if (dp) {
                LDRD(V0, CPU(d[vd]));
                LDRD(V1, CPU(d[vn]));
                LDRD(V2, CPU(d[vm]));
            } else {
                LDRS(V0, CPU(s[vd]));
                LDRS(V1, CPU(s[vn]));
                LDRS(V2, CPU(s[vm]));
            }
            FPDATAPROC3SOURCE(dp, 0, 0, cpopc, cpopc ^ op, V0, V1, V2, V0);
            if (dp) {
                STRD(V0, CPU(d[vd]));
            } else {
                STRS(V0, CPU(s[vd]));
            }
            break;
        case 2:
        case 3:
        case 8: {
            if (dp) {
                LDRD(V1, CPU(d[vn]));
                LDRD(V2, CPU(d[vm]));
            } else {
                LDRS(V1, CPU(s[vn]));
                LDRS(V2, CPU(s[vm]));
            }
            u32 opcode = cpopc == 8 ? 1 : cpopc == 2 ? op * 8 : 2 + op;
            FPDATAPROC2SOURCE(dp, 0, 0, opcode, V0, V1, V2);
            if (dp) {
                STRD(V0, CPU(d[vd]));
            } else {
                STRS(V0, CPU(s[vd]));
            }
            break;
        }
        case 11: {
            op = instr.cp_data_proc.cp & 4;
            u32 crn = instr.cp_data_proc.crn;
            switch (crn) {
                case 0:
                case 1:
                    if (dp) {
                        LDRD(V2, CPU(d[vm]));
                    } else {
                        LDRS(V2, CPU(s[vm]));
                    }
                    FPDATAPROC1SOURCE(dp, 0, 0, crn << 1 | op, V0, V2);
                    if (dp) {
                        STRD(V0, CPU(d[vd]));
                    } else {
                        STRS(V0, CPU(s[vd]));
                    }
                    break;
                case 4:
                case 5:
                    if (dp) {
                        LDRD(V0, CPU(d[vd]));
                        LDRD(V2, CPU(d[vm]));
                    } else {
                        LDRS(V0, CPU(s[vd]));
                        LDRS(V2, CPU(s[vm]));
                    }
                    FPCOMPARE(dp, 0, 0, 0, (crn & 1) * 8, V0, V2);
                    MRS(R0, NZCV);
                    LDR(R1, CPU(fpscr));
                    UBFX(R0, R0, 28, 4);
                    BFI(R1, R0, 28, 4);
                    STR(R1, CPU(fpscr));
                    break;
                case 7:
                    if (dp) {
                        vd = vd << 1 | ((instr.cp_data_proc.cpopc >> 2) & 1);
                        LDRD(V0, CPU(d[vm]));
                        FCVTSD(V0, V0);
                        STRS(V0, CPU(s[vd]));
                    } else {
                        vd = vd >> 1;
                        LDRS(V0, CPU(s[vm]));
                        FCVTDS(V0, V0);
                        STRD(V0, CPU(d[vd]));
                    }
                    break;
                case 8:
                    if (dp) vm = vm << 1 | (instr.cp_data_proc.cp & 1);
                    LDR(R0, CPU(s[vm]));
                    FPCONVERTINTVR(0, dp, 0, 0, 2 + !op, V0, R0);
                    if (dp) {
                        STRD(V0, CPU(d[vd]));
                    } else {
                        STRS(V0, CPU(s[vd]));
                    }
                    break;
                case 12:
                case 13:
                    if (dp) {
                        vd = vd << 1 | ((instr.cp_data_proc.cpopc >> 2) & 1);
                        LDRD(V0, CPU(d[vm]));
                    } else {
                        LDRS(V0, CPU(s[vm]));
                    }
                    FPCONVERTINTRV(0, dp, 0, 3, !(crn & 1), R0, V0);
                    STR(R0, CPU(s[vd]));
                    if (!op) lwarnonce("unimpl rounding mode");
                    break;
            }
            break;
        }
    }
}

static void compileVFPLoadMem(ArmCodeBackend* backend, ArmInstr instr,
                              rasA64Reg addr, rasLabel lldf32, rasLabel lldf64) {
    u32 rcount;
    if (instr.cp_data_trans.p && !instr.cp_data_trans.w) {
        rcount = 1;
    } else {
        rcount = instr.cp_data_trans.offset;
        if (instr.cp_data_trans.cpnum & 1) rcount >>= 1;
    }

    u32 vd = instr.cp_data_trans.crd;

    if (rcount > 1) {
        PUSH(R19, ZR);
        MOV(R19, addr);
        addr = R19;
    }

    if (instr.cp_data_trans.cpnum & 1) {
        for (int i = 0; i < rcount; i++) {
            MOVX(R0, R29);
            MOV(R1, addr);
            LDRLX(IP0, lldf64);
            BLR(IP0);
            STRD(V0, CPU(d[(vd + i) & 15]));
            if (i < rcount - 1) ADD(addr, addr, 8);
        }
    } else {
        vd = vd << 1 | instr.cp_data_trans.n;

        for (int i = 0; i < rcount; i++) {
            MOVX(R0, R29);
            MOV(R1, addr);
            LDRLX(IP0, lldf32);
            BLR(IP0);
            STRS(V0, CPU(s[(vd + i) & 31]));
            if (i < rcount - 1) ADD(addr, addr, 4);
        }
    }

    if (rcount > 1) {
        POP(R19, ZR);
    }
}

static void compileVFPStoreMem(ArmCodeBackend* backend, ArmInstr instr,
                               rasA64Reg addr, rasLabel lstf32, rasLabel lstf64) {
    u32 rcount;
    if (instr.cp_data_trans.p && !instr.cp_data_trans.w) {
        rcount = 1;
    } else {
        rcount = instr.cp_data_trans.offset;
        if (instr.cp_data_trans.cpnum & 1) rcount >>= 1;
    }

    u32 vd = instr.cp_data_trans.crd;

    if (rcount > 1) {
        PUSH(R19, ZR);
        MOV(R19, addr);
        addr = R19;
    }

    if (instr.cp_data_trans.cpnum & 1) {
        for (int i = 0; i < rcount; i++) {
            MOVX(R0, R29);
            MOV(R1, addr);
            LDRD(V0, CPU(d[(vd + i) & 15]));
            LDRLX(IP0, lstf64);
            BLR(IP0);
            if (i < rcount - 1) ADD(addr, addr, 8);
        }
    } else {
        vd = vd << 1 | instr.cp_data_trans.n;

        for (int i = 0; i < rcount; i++) {
            MOVX(R0, R29);
            MOV(R1, addr);
            LDRS(V0, CPU(s[(vd + i) & 31]));
            LDRLX(IP0, lstf32);
            BLR(IP0);
            if (i < rcount - 1) ADD(addr, addr, 4);
        }
    }

    if (rcount > 1) {
        POP(R19, ZR);
    }
}

static void compileVFPRead(ArmCodeBackend* backend, ArmInstr instr,
                           rasA64Reg dst) {
    if (instr.cp_reg_trans.cpopc == 7) {
        if (instr.cp_reg_trans.crn == 1) {
            LDR(dst, CPU(fpscr));
        } else {
            lwarn("unknown vfp special reg %d", instr.cp_reg_trans.crn);
            MOV(dst, 0);
        }
        return;
    }

    u32 vn = instr.cp_reg_trans.crn << 1;
    if (instr.cp_reg_trans.cpnum & 1) vn |= instr.cp_reg_trans.cpopc & 1;
    else vn |= instr.cp_reg_trans.cp >> 2;

    LDR(dst, CPU(s[vn]));
}

static void compileVFPWrite(ArmCodeBackend* backend, ArmInstr instr,
                            rasA64Reg src) {
    if (instr.cp_reg_trans.cpopc == 7) {
        if (instr.cp_reg_trans.crn == 1) {
            STR(src, CPU(fpscr));
        } else {
            lwarn("unknown vfp special reg %d", instr.cp_reg_trans.crn);
        }
        return;
    }

    u32 vn = instr.cp_reg_trans.crn << 1;
    if (instr.cp_reg_trans.cpnum & 1) vn |= instr.cp_reg_trans.cpopc & 1;
    else vn |= instr.cp_reg_trans.cp >> 2;

    STR(src, CPU(s[vn]));
}

static void compileVFPRead64(ArmCodeBackend* backend, ArmInstr instr,
                             rasA64Reg dst, bool h) {
    if (instr.cp_double_reg_trans.cpnum & 1) {
        u32 vm = instr.cp_double_reg_trans.crm;
        if (h) {
            LDR(dst, CPU(d[vm], 4));
        } else {
            LDR(dst, CPU(d[vm]));
        }
    } else {
        u32 vm = instr.cp_double_reg_trans.crm << 1 |
                 ((instr.cp_double_reg_trans.cp >> 1) & 1);
        if (h) {
            LDR(dst, CPU(s[vm + 1]));
        } else {
            LDR(dst, CPU(s[vm]));
        }
    }
}

static void compileVFPWrite64(ArmCodeBackend* backend, ArmInstr instr,
                              rasA64Reg src, bool h) {
    if (instr.cp_double_reg_trans.cpnum & 1) {
        u32 vm = instr.cp_double_reg_trans.crm;
        if (h) {
            STR(src, CPU(d[vm], 4));
        } else {
            STR(src, CPU(d[vm]));
        }
    } else {
        u32 vm = instr.cp_double_reg_trans.crm << 1 |
                 ((instr.cp_double_reg_trans.cp >> 1) & 1);
        if (h) {
            if (vm < 31) STR(src, CPU(s[vm + 1]));
        } else {
            STR(src, CPU(s[vm]));
        }
    }
}

void backend_arm_free(ArmCodeBackend* backend) {
    hostregalloc_free(&backend->hralloc);
    Vec_free(backend->links);
    rasDestroy(backend->code);
    free(backend);
}

JITFunc backend_arm_get_code(ArmCodeBackend* backend) {
    return rasGetCode(backend->code);
}

void backend_arm_patch_links(JITBlock* block) {
    ArmCodeBackend* backend = block->backend;
    for (int i = 0; i < backend->links.size; i++) {
        auto patch = backend->links.d[i];
        JITBlock* linkblock =
            get_jitblock(backend->cpu, patch.attrs, patch.addr);
        rasDefineLabelExternal(patch.lab, linkblock->code);
        Vec_push(linkblock->linkingblocks,
                 ((BlockLocation) {block->attrs, block->start_addr}));
    }
    rasReady(backend->code);
    Vec_free(backend->links);
}

#ifndef NOCAPSTONE
void backend_arm_disassemble(ArmCodeBackend* backend) {
    print_hostregs(&backend->hralloc);
    csh handle;
    cs_insn* insn;
    cs_open(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, &handle);
    cs_option(handle, CS_OPT_SKIPDATA, CS_OPT_ON);
    size_t count = cs_disasm(handle, rasGetCode(backend->code),
                             rasGetSize(backend->code), 0, 0, &insn);
    printf("--------- JIT Disassembly at %p ------------\n",
           rasGetCode(backend->code));
    for (size_t i = 0; i < count; i++) {
        printf("%04lx: %08x\t%s %s\n", insn[i].address, *(u32*) &insn[i].bytes,
               insn[i].mnemonic, insn[i].op_str);
    }
    cs_free(insn, count);
    cs_close(&handle);
}
#endif

#endif
