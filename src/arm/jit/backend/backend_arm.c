#ifdef __aarch64__

#include "backend_arm.h"

#ifndef NOCAPSTONE
#include <capstone/capstone.h>
#endif

#include "../../media.h"
#include "../../vfp.h"

#define RAS_MACROS
#define RAS_DEFAULT_SUFFIX w
#define RAS_CTX_VAR backend->code
#include <ras/ras.h>

#define TEMPREGS_BASE 3
#define TEMPREGS_COUNT 13
#define SAVEDREGS_BASE 19
#define SAVEDREGS_COUNT 10

void ras_error_cb(rasError err) {
    lerror("ras: %s", rasErrorStrings[err]);
    exit(1);
}

[[gnu::constructor]] static void init_error() {
    rasSetErrorCallback((rasErrorCallback) ras_error_cb, nullptr);
}

// returns:
// 0-31 : reg index
// 32+n : stack index
int getOpForReg(HostRegAllocation* hralloc, int i) {
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

void print_hostregs(HostRegAllocation* hralloc) {
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

int getOp(ArmCodeBackend* backend, int i) {
    int assn = backend->regalloc->reg_assn[i];
    if (assn == -1) return -1;
    else return getOpForReg(&backend->hralloc, assn);
}

#define GETOP(i) getOp(backend, i)

#define CPU(m, ...) (r29, offsetof(ArmCore, m) __VA_OPT__(+) __VA_ARGS__)

#define LOADOP(i, flbk)                                                        \
    ({                                                                         \
        auto dst = flbk;                                                       \
        if (inst.imm##i) {                                                     \
            mov(dst, inst.op##i);                                              \
        } else {                                                               \
            int op = GETOP(inst.op##i);                                        \
            if (op >= 32) {                                                    \
                op -= 32;                                                      \
                ldr(dst, (sp, 4 * op));                                        \
            } else {                                                           \
                dst = Reg(op);                                                 \
            }                                                                  \
        }                                                                      \
        dst;                                                                   \
    })
#define LOADOP1() LOADOP(1, ip0)
#define LOADOP2() LOADOP(2, ip1)

#define MOVOP(i, dst)                                                          \
    ({                                                                         \
        auto src = LOADOP(i, dst);                                             \
        if (src.idx != dst.idx) mov(dst, src);                                 \
    })
#define MOVOP1(dst) MOVOP(1, dst)
#define MOVOP2(dst) MOVOP(2, dst)

#define DSTREG()                                                               \
    ({                                                                         \
        int op = GETOP(i);                                                     \
        (op >= 32) ? ip0 : Reg(op);                                            \
    })
#define STOREDST()                                                             \
    ({                                                                         \
        int op = GETOP(i);                                                     \
        if (op >= 32) {                                                        \
            op -= 32;                                                          \
            str(ip0, (sp, 4 * op));                                            \
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

    Label(lcp15r, cpu->cp15_read);
    Label(lcp15w, cpu->cp15_write);
    Label(lld8, cpu->read8);
    Label(lld16, cpu->read16);
    Label(lld32, cpu->read32);
    Label(lst8, cpu->write8);
    Label(lst16, cpu->write16);
    Label(lst32, cpu->write32);

    Label(looplabel);

    rasLabel labels[MAX_BLOCK_INSTRS];
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
                ldr(dst, CPU(r[inst.op1]));
                break;
            }
            case IR_STORE_REG: {
                auto src = LOADOP2();
                str(src, CPU(r[inst.op1]));
                break;
            }
            case IR_LOAD_REG_USR: {
                auto dst = DSTREG();
                int rd = inst.op1;
                if (rd < 13) {
                    ldr(dst, CPU(banked_r8_12[0][rd - 8]));
                } else if (rd == 13) {
                    ldr(dst, CPU(banked_sp[0]));
                } else if (rd == 14) {
                    ldr(dst, CPU(banked_lr[0]));
                }
                break;
            }
            case IR_STORE_REG_USR: {
                auto src = LOADOP2();
                int rd = inst.op1;
                if (rd < 13) {
                    str(src, CPU(banked_r8_12[0][rd - 8]));
                } else if (rd == 13) {
                    str(src, CPU(banked_sp[0]));
                } else if (rd == 14) {
                    str(src, CPU(banked_lr[0]));
                }
                break;
            }
            case IR_LOAD_FLAG: {
                auto dst = DSTREG();
                ldr(dst, CPU(cpsr));
                ubfx(dst, dst, 31 - inst.op1, 1);
                break;
            }
            case IR_STORE_FLAG: {
                if (ir->code.d[i - 2].opcode != IR_STORE_FLAG) {
                    mov(r0, 0);
                    flags_mask = 0;
                }
                if (inst.imm2) {
                    if (inst.op2) orr(r0, r0, BIT(31 - inst.op1));
                } else {
                    auto src = LOADOP2();
                    bfi(r0, src, 31 - inst.op1, 1);
                }
                flags_mask |= BIT(31 - inst.op1);
                if (ir->code.d[i + 2].opcode != IR_STORE_FLAG) {
                    ldr(r1, CPU(cpsr));
                    and(r1, r1, ~flags_mask, r2);
                    orr(r1, r1, r0);
                    str(r1, CPU(cpsr));
                }
                break;
            }
            case IR_LOAD_CPSR: {
                auto dst = DSTREG();
                ldr(dst, CPU(cpsr));
                break;
            }
            case IR_STORE_CPSR: {
                auto src = LOADOP2();
                str(src, CPU(cpsr));
                break;
            }
            case IR_LOAD_SPSR: {
                auto dst = DSTREG();
                ldr(dst, CPU(spsr));
                break;
            }
            case IR_STORE_SPSR: {
                auto src = LOADOP2();
                str(src, CPU(spsr));
                break;
            }
            case IR_LOAD_THUMB: {
                auto dst = DSTREG();
                ldr(dst, CPU(cpsr));
                ubfx(dst, dst, 5, 1);
                break;
            }
            case IR_STORE_THUMB: {
                auto src = LOADOP2();
                ldr(r0, CPU(cpsr));
                bfi(r0, src, 5, 1);
                str(r0, CPU(cpsr));
                break;
            }
            case IR_VFP_LOAD_MEM: {
                movx(r0, r29);
                mov(r1, inst.op1);
                MOVOP2(r2);
                adrl(ip0, Lnew(exec_vfp_load_mem));
                blr(ip0);
                break;
            }
            case IR_VFP_STORE_MEM: {
                movx(r0, r29);
                mov(r1, inst.op1);
                MOVOP2(r2);
                adrl(ip0, Lnew(exec_vfp_store_mem));
                blr(ip0);
                break;
            }
            // case IR_VFP_DATA_PROC: {
            //     compileVFPDataProc(ArmInstr(inst.op1));
            //     break;
            // }
            // case IR_VFP_LOAD_MEM: {
            //     auto addr = LOADOP2();
            //     compileVFPLoadMem(ArmInstr(inst.op1), addr);
            //     break;
            // }
            // case IR_VFP_STORE_MEM: {
            //     auto addr = LOADOP2();
            //     compileVFPStoreMem(ArmInstr(inst.op1), addr);
            //     break;
            // }
            // case IR_VFP_READ: {
            //     auto dst = DSTREG();
            //     compileVFPRead(ArmInstr(inst.op1), dst);
            //     break;
            // }
            // case IR_VFP_WRITE: {
            //     auto src = LOADOP2();
            //     compileVFPWrite(ArmInstr(inst.op1), src);
            //     break;
            // }
            // case IR_VFP_READ64L: {
            //     auto dst = DSTREG();
            //     compileVFPRead64(ArmInstr(inst.op1), dst, false);
            //     break;
            // }
            // case IR_VFP_READ64H: {
            //     auto dst = DSTREG();
            //     compileVFPRead64(ArmInstr(inst.op1), dst, true);
            //     break;
            // }
            // case IR_VFP_WRITE64L: {
            //     auto src = LOADOP2();
            //     compileVFPWrite64(ArmInstr(inst.op1), src, false);
            //     break;
            // }
            // case IR_VFP_WRITE64H: {
            //     auto src = LOADOP2();
            //     compileVFPWrite64(ArmInstr(inst.op1), src, true);
            //     break;
            // }
            case IR_CP15_READ: {
                auto dst = DSTREG();
                movx(r0, r29);
                mov(r1, inst.op1);
                adrl(ip0, lcp15r);
                blr(ip0);
                mov(dst, r0);
                break;
            }
            case IR_CP15_WRITE: {
                auto src = LOADOP2();
                movx(r0, r29);
                mov(r1, inst.op1);
                mov(r2, src);
                adrl(ip0, lcp15w);
                blr(ip0);
                break;
            }
            case IR_LOAD_MEM8: {
                auto dst = DSTREG();
                movx(r0, r29);
                MOVOP1(r1);
                mov(r2, 0);
                adrl(ip0, lld8);
                blr(ip0);
                mov(dst, r0);
                break;
            }
            case IR_LOAD_MEMS8: {
                auto dst = DSTREG();
                movx(r0, r29);
                MOVOP1(r1);
                mov(r2, 1);
                adrl(ip0, lld8);
                blr(ip0);
                mov(dst, r0);
                break;
            }
            case IR_LOAD_MEM16: {
                auto dst = DSTREG();
                movx(r0, r29);
                MOVOP1(r1);
                mov(r2, 0);
                adrl(ip0, lld16);
                blr(ip0);
                mov(dst, r0);
                break;
            }
            case IR_LOAD_MEMS16: {
                auto dst = DSTREG();
                movx(r0, r29);
                MOVOP1(r1);
                mov(r2, 1);
                adrl(ip0, lld16);
                blr(ip0);
                mov(dst, r0);
                break;
            }
            case IR_LOAD_MEM32: {
                auto dst = DSTREG();
                movx(r0, r29);
                MOVOP1(r1);
                adrl(ip0, lld32);
                blr(ip0);
                mov(dst, r0);
                break;
            }
            case IR_STORE_MEM8: {
                movx(r0, r29);
                MOVOP1(r1);
                MOVOP2(r2);
                adrl(ip0, lst8);
                blr(ip0);
                break;
            }
            case IR_STORE_MEM16: {
                movx(r0, r29);
                MOVOP1(r1);
                MOVOP2(r2);
                adrl(ip0, lst16);
                blr(ip0);
                break;
            }
            case IR_STORE_MEM32: {
                movx(r0, r29);
                MOVOP1(r1);
                MOVOP2(r2);
                adrl(ip0, lst32);
                blr(ip0);
                break;
            }
            case IR_MOV: {
                auto src = LOADOP2();
                auto dst = DSTREG();
                mov(dst, src);
                break;
            }
            case IR_AND: {
                if (inst.imm2) {
                    auto src1 = LOADOP1();
                    auto dst = DSTREG();
                    ands(dst, src1, inst.op2, ip1);
                } else if (inst.imm1) {
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    ands(dst, src2, inst.op1, ip0);
                } else {
                    auto src1 = LOADOP1();
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    ands(dst, src1, src2);
                }
                lastflags = i;
                break;
            }
            case IR_OR: {
                if (inst.imm2) {
                    auto src1 = LOADOP1();
                    auto dst = DSTREG();
                    orr(dst, src1, inst.op2, ip1);
                } else if (inst.imm1) {
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    orr(dst, src2, inst.op1, ip0);
                } else {
                    auto src1 = LOADOP1();
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    orr(dst, src1, src2);
                }
                break;
            }
            case IR_XOR: {
                if (inst.imm2) {
                    auto src1 = LOADOP1();
                    auto dst = DSTREG();
                    eor(dst, src1, inst.op2, ip1);
                } else if (inst.imm1) {
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    eor(dst, src2, inst.op1, ip0);
                } else {
                    auto src1 = LOADOP1();
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    eor(dst, src1, src2);
                }
                break;
            }
            case IR_NOT: {
                auto src = LOADOP2();
                auto dst = DSTREG();
                mvn(dst, src);
                break;
            }
            case IR_LSL: {
                auto src = LOADOP1();
                auto dst = DSTREG();
                if (inst.imm2) {
                    if (inst.op2 >= 32) {
                        mov(dst, 0);
                    } else {
                        lsl(dst, src, inst.op2);
                    }
                } else {
                    auto shift = LOADOP2();
                    Label(lelse);
                    Label(lendif);
                    cmp(shift, 32);
                    bhs(lelse);
                    lsl(dst, src, shift);
                    b(lendif);
                    L(lelse);
                    mov(dst, 0);
                    L(lendif);

                    lastflags = 0;
                }
                break;
            }
            case IR_LSR: {
                auto src = LOADOP1();
                auto dst = DSTREG();
                if (inst.imm2) {
                    if (inst.op2 >= 32) {
                        mov(dst, 0);
                    } else {
                        lsr(dst, src, inst.op2);
                    }
                } else {
                    auto shift = LOADOP2();
                    Label(lelse);
                    Label(lendif);
                    cmp(shift, 32);
                    bhs(lelse);
                    lsr(dst, src, shift);
                    b(lendif);
                    L(lelse);
                    mov(dst, 0);
                    L(lendif);

                    lastflags = 0;
                }
                break;
            }
            case IR_ASR: {
                auto src = LOADOP1();
                auto dst = DSTREG();
                if (inst.imm2) {
                    if (inst.op2 >= 32) {
                        asr(dst, src, 31);
                    } else {
                        asr(dst, src, inst.op2);
                    }
                } else {
                    auto shift = LOADOP2();
                    Label(lelse);
                    Label(lendif);
                    cmp(shift, 32);
                    bhs(lelse);
                    asr(dst, src, shift);
                    b(lendif);
                    L(lelse);
                    asr(dst, src, 31);
                    L(lendif);

                    lastflags = 0;
                }
                break;
            }

            case IR_ROR: {
                auto src = LOADOP1();
                auto dst = DSTREG();
                if (inst.imm2) {
                    ror(dst, src, inst.op2 % 32);
                } else {
                    auto shift = LOADOP2();
                    ror(dst, src, shift);
                }
                break;
            }
            case IR_RRC: {
                auto src = LOADOP1();
                auto dst = DSTREG();
                cset(r0, cs);
                extr(dst, r0, src, 1);
                break;
            }
            case IR_ADD: {
                if (inst.imm2) {
                    auto src1 = LOADOP1();
                    auto dst = DSTREG();
                    adds(dst, src1, inst.op2, ip1);
                } else if (inst.imm1) {
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    adds(dst, src2, inst.op1, ip0);
                } else {
                    auto src1 = LOADOP1();
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    adds(dst, src1, src2);
                }
                lastflags = i;
                break;
            }
            case IR_SUB: {
                if (inst.imm2) {
                    auto src1 = LOADOP1();
                    auto dst = DSTREG();
                    subs(dst, src1, inst.op2, ip1);
                } else {
                    auto src1 = LOADOP1();
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    subs(dst, src1, src2);
                }
                lastflags = i;
                break;
            }
            case IR_ADC: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                adcs(dst, src1, src2);
                lastflags = i;
                break;
            }
            case IR_SBC: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                sbcs(dst, src1, src2);
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
                        smull(dst, src1, src2);
                    } else {
                        umull(dst, src1, src2);
                    }
                    STOREDST();
                    i++;
                    auto dsth = DSTREG();
                    lsrx(dsth, dst, 32);
                } else {
                    auto src1 = LOADOP1();
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    mul(dst, src1, src2);
                }
                break;
            }
            case IR_SMULH: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                smull(dst, src1, src2);
                lsrx(dst, dst, 32);
                break;
            }
            case IR_UMULH: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                umull(dst, src1, src2);
                lsrx(dst, dst, 32);
                break;
            }
            case IR_SMULW: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                smull(dst, src1, src2);
                lsrx(dst, dst, 16);
                break;
            }
            case IR_CLZ: {
                auto src = LOADOP2();
                auto dst = DSTREG();
                clz(dst, src);
                break;
            }
            case IR_REV: {
                auto src = LOADOP2();
                auto dst = DSTREG();
                rev(dst, src);
                break;
            }
            case IR_REV16: {
                auto src = LOADOP2();
                auto dst = DSTREG();
                rev16(dst, src);
                break;
            }
            case IR_USAT: {
                auto src = LOADOP2();
                auto dst = DSTREG();
                cmp(src, 0);
                csel(dst, zr, src, lt);
                mov(r0, MASK(inst.op1));
                cmp(src, r0);
                cmov(dst, r0, gt);
                break;
            }
            case IR_SSAT: {
                auto src = LOADOP2();
                auto dst = DSTREG();
                mov(r0, ~MASK(inst.op1));
                cmp(src, r0);
                csel(dst, r0, src, lt);
                mov(r0, MASK(inst.op1));
                cmp(src, r0);
                cmov(dst, r0, gt);
                break;
            }
            case IR_MEDIA_UADD8: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                movx(r0, r29);
                mov(r1, src1);
                mov(r2, src2);
                adrl(ip0, Lnew(media_uadd8));
                blr(ip0);
                mov(dst, r0);
                break;
            }
            case IR_MEDIA_USUB8: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                movx(r0, r29);
                mov(r1, src1);
                mov(r2, src2);
                adrl(ip0, Lnew(media_usub8));
                blr(ip0);
                mov(dst, r0);
                break;
            }
            // case IR_MEDIA_UQADD8: {
            //     auto src1 = LOADOP1();
            //     auto src2 = LOADOP2();
            //     auto dst = DSTREG();
            //     mov(v0.s[0], src1);
            //     mov(v1.s[0], src2);
            //     uqadd(v0.b8, v0.b8, v1.b8);
            //     mov(dst, v0.s[0]);
            //     break;
            // }
            // case IR_MEDIA_UQSUB8: {
            //     auto src1 = LOADOP1();
            //     auto src2 = LOADOP2();
            //     auto dst = DSTREG();
            //     mov(v0.s[0], src1);
            //     mov(v1.s[0], src2);
            //     uqsub(v0.b8, v0.b8, v1.b8);
            //     mov(dst, v0.s[0]);
            //     break;
            // }
            // case IR_MEDIA_UHADD8: {
            //     auto src1 = LOADOP1();
            //     auto src2 = LOADOP2();
            //     auto dst = DSTREG();
            //     mov(v0.s[0], src1);
            //     mov(v1.s[0], src2);
            //     uhadd(v0.b8, v0.b8, v1.b8);
            //     mov(dst, v0.s[0]);
            //     break;
            // }
            case IR_MEDIA_SSUB8: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                movx(r0, r29);
                mov(r1, src1);
                mov(r2, src2);
                adrl(ip0, Lnew(media_ssub8));
                blr(ip0);
                mov(dst, r0);
                break;
            }
            // case IR_MEDIA_QSUB8: {
            //     auto src1 = LOADOP1();
            //     auto src2 = LOADOP2();
            //     auto dst = DSTREG();
            //     mov(v0.s[0], src1);
            //     mov(v1.s[0], src2);
            //     sqsub(v0.b8, v0.b8, v1.b8);
            //     mov(dst, v0.s[0]);
            //     break;
            // }
            case IR_MEDIA_SEL: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                movx(r0, r29);
                mov(r1, src1);
                mov(r2, src2);
                adrl(ip0, Lnew(media_sel));
                blr(ip0);
                mov(dst, r0);
                break;
            }
            case IR_GETN: {
                auto dst = DSTREG();
                if (inst.imm2) {
                    mov(dst, inst.op2 >> 31);
                } else {
                    if (lastflags != inst.op2) {
                        auto src = LOADOP2();
                        tst(src, src);
                        lastflags = inst.op2;
                    }
                    cset(dst, mi);
                }
                break;
            }
            case IR_GETZ: {
                auto dst = DSTREG();
                if (inst.imm2) {
                    mov(dst, inst.op2 == 0);
                } else {
                    if (lastflags != inst.op2) {
                        auto src = LOADOP2();
                        tst(src, src);
                        lastflags = inst.op2;
                    }
                    cset(dst, eq);
                }
                break;
            }
            case IR_GETC: {
                auto dst = DSTREG();
                cset(dst, cs);
                break;
            }
            case IR_SETC: {
                auto src = LOADOP2();
                cmp(src, 1);
                lastflags = 0;
                break;
            }
            case IR_GETCIFZ: {
                auto cond = LOADOP1();
                auto src = LOADOP2();
                auto dst = DSTREG();
                cset(r0, cs);
                tst(cond, cond);
                csel(dst, r0, src, eq);
                lastflags = 0;
                break;
            }
            case IR_GETV: {
                auto dst = DSTREG();
                cset(dst, vs);
                break;
            }
            case IR_PCMASK: {
                auto src = LOADOP1();
                auto dst = DSTREG();
                lsl(dst, src, 1);
                sub(dst, dst, 4);
                break;
            }
            case IR_JZ: {
                jmptarget = inst.op2;
                labels[nlabel++] = Lnew();
                auto cond = LOADOP1();
                cbz(cond, labels[nlabel - 1]);
                break;
            }
            case IR_JNZ: {
                jmptarget = inst.op2;
                labels[nlabel++] = Lnew();
                auto cond = LOADOP1();
                cbnz(cond, labels[nlabel - 1]);
                break;
            }
            case IR_JELSE: {
                jmptarget = inst.op2;
                labels[nlabel++] = Lnew();
                b(labels[nlabel - 1]);
                L(labels[nlabel - 2]);
                break;
            }
            case IR_MODESWITCH: {
                movx(r0, r29);
                mov(r1, inst.op1);
                adrl(ip0, Lnew(cpu_update_mode));
                blr(ip0);
                break;
            }
            case IR_EXCEPTION: {
                switch (inst.op1) {
                    case E_SWI:
                        movx(r0, r29);
                        mov(r1, (ArmInstr) {inst.op2}.sw_intr.arg);
                        adrl(ip0, Lnew(cpu->handle_svc));
                        blr(ip0);
                        break;
                    case E_UND:
                        movx(r0, r29);
                        mov(r1, inst.op2);
                        adrl(ip0, Lnew(cpu_undefined_fail));
                        blr(ip0);
                        break;
                }
                break;
            }
            case IR_WFE: {
                mov(r0, 1);
                strb(r0, CPU(wfe));
                break;
            }
            case IR_BEGIN: {

                push(fp, lr);
                for (int i = 0; i < backend->hralloc.count[REG_SAVED]; i += 2) {
                    push(Reg(SAVEDREGS_BASE + i), Reg(SAVEDREGS_BASE + i + 1));
                }
                int spdisp = SPDISP();
                if (spdisp) sub(sp, sp, spdisp);

                adrl(r29, Lnew(cpu));
                L(looplabel);

                break;
            }
            case IR_END_RET:
            case IR_END_LINK:
            case IR_END_LOOP: {
                lastflags = 0;

                ldrx(r0, CPU(cycles));
                subx(r0, r0, inst.cycles);
                strx(r0, CPU(cycles));

                if (inst.opcode == IR_END_LOOP) {
                    cmpx(r0, 0);
                    bgt(looplabel);
                }

                int spdisp = SPDISP();
                if (spdisp) add(sp, sp, spdisp);
                for (int i = (backend->hralloc.count[REG_SAVED] - 1) & ~1;
                     i >= 0; i -= 2) {
                    pop(Reg(SAVEDREGS_BASE + i), Reg(SAVEDREGS_BASE + i + 1));
                }
                pop(fp, lr);

                // if (inst.opcode == IR_END_LINK) {
                //     Label nolink, linkaddr;
                //     cmp(x0, 0);
                //     ble(nolink);
                //     ldr(x16, linkaddr);
                //     br(x16);
                //     align(8);
                //     L(linkaddr);
                //     links.push_back((LinkPatch) {(u32) (getCurr() -
                //     getCode()),
                //                                  inst.op1, inst.op2});
                //     dd(0);
                //     dd(0);
                //     L(nolink);
                // }

                ret();
                break;
            }
            default:
                break;
        }
        STOREDST();
    }

    return backend;
}

void backend_arm_free(ArmCodeBackend* backend) {
    hostregalloc_free(&backend->hralloc);
    rasDestroy(backend->code);
    free(backend);
}

JITFunc backend_arm_get_code(ArmCodeBackend* backend) {
    return rasGetCode(backend->code);
}

void backend_arm_patch_links(JITBlock* block) {
    ArmCodeBackend* backend = block->backend;
    rasReady(backend->code);
}

#ifndef NOCAPSTONE
void backend_arm_disassemble(ArmCodeBackend* backend) {
    print_hostregs(&backend->hralloc);
    csh handle;
    cs_insn* insn;
    cs_open(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, &handle);
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
