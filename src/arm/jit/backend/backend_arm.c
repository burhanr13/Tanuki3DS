#ifdef __aarch64__

#include "backend_arm.h"

#ifndef NOCAPSTONE
#include <capstone/capstone.h>
#endif

#include "arm/media.h"

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

void compileVFPDataProc(ArmCodeBackend* backend, ArmInstr instr);
void compileVFPLoadMem(ArmCodeBackend* backend, ArmInstr instr, rasReg addr,
                       rasLabel lldf32, rasLabel lldf64);
void compileVFPStoreMem(ArmCodeBackend* backend, ArmInstr instr, rasReg addr,
                        rasLabel lstf32, rasLabel lstf64);
void compileVFPRead(ArmCodeBackend* backend, ArmInstr instr, rasReg dst);
void compileVFPWrite(ArmCodeBackend* backend, ArmInstr instr, rasReg src);
void compileVFPRead64(ArmCodeBackend* backend, ArmInstr instr, rasReg dst,
                      bool h);
void compileVFPWrite64(ArmCodeBackend* backend, ArmInstr instr, rasReg src,
                       bool h);

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

    // callback address literal pool
    Label(lld8);
    Label(lld16);
    Label(lld32);
    Label(lst8);
    Label(lst16);
    Label(lst32);
    Label(lldf32);
    Label(lldf64);
    Label(lstf32);
    Label(lstf64);

    // label for END_LOOP (jump to the same the block)
    Label(looplabel);

    // labels for jump instructions
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
                movx(r0, r29);
                mov(r1, inst.op1);
                movx(ip0, (uintptr_t) cpu->cp15_read);
                blr(ip0);
                mov(dst, r0);
                break;
            }
            case IR_CP15_WRITE: {
                auto src = LOADOP2();
                movx(r0, r29);
                mov(r1, inst.op1);
                mov(r2, src);
                movx(ip0, (uintptr_t) cpu->cp15_write);
                blr(ip0);
                break;
            }
            case IR_LOAD_MEM8: {
                auto dst = DSTREG();
                movx(r0, r29);
                MOVOP1(r1);
                mov(r2, 0);
                ldrlx(ip0, lld8);
                blr(ip0);
                mov(dst, r0);
                break;
            }
            case IR_LOAD_MEMS8: {
                auto dst = DSTREG();
                movx(r0, r29);
                MOVOP1(r1);
                mov(r2, 1);
                ldrlx(ip0, lld8);
                blr(ip0);
                mov(dst, r0);
                break;
            }
            case IR_LOAD_MEM16: {
                auto dst = DSTREG();
                movx(r0, r29);
                MOVOP1(r1);
                mov(r2, 0);
                ldrlx(ip0, lld16);
                blr(ip0);
                mov(dst, r0);
                break;
            }
            case IR_LOAD_MEMS16: {
                auto dst = DSTREG();
                movx(r0, r29);
                MOVOP1(r1);
                mov(r2, 1);
                ldrlx(ip0, lld16);
                blr(ip0);
                mov(dst, r0);
                break;
            }
            case IR_LOAD_MEM32: {
                auto dst = DSTREG();
                movx(r0, r29);
                MOVOP1(r1);
                ldrlx(ip0, lld32);
                blr(ip0);
                mov(dst, r0);
                break;
            }
            case IR_STORE_MEM8: {
                movx(r0, r29);
                MOVOP1(r1);
                MOVOP2(r2);
                ldrlx(ip0, lst8);
                blr(ip0);
                break;
            }
            case IR_STORE_MEM16: {
                movx(r0, r29);
                MOVOP1(r1);
                MOVOP2(r2);
                ldrlx(ip0, lst16);
                blr(ip0);
                break;
            }
            case IR_STORE_MEM32: {
                movx(r0, r29);
                MOVOP1(r1);
                MOVOP2(r2);
                ldrlx(ip0, lst32);
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
                auto dst = DSTREG();
                movx(r0, r29);
                MOVOP1(r1);
                MOVOP2(r2);
                movx(ip0, (uintptr_t) media_uadd8);
                blr(ip0);
                mov(dst, r0);
                break;
            }
            case IR_MEDIA_USUB8: {
                auto dst = DSTREG();
                movx(r0, r29);
                MOVOP1(r1);
                MOVOP2(r2);
                movx(ip0, (uintptr_t) media_usub8);
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
            case IR_MEDIA_UQSUB8: {
                auto dst = DSTREG();
                movx(r0, r29);
                MOVOP1(r1);
                MOVOP2(r2);
                movx(ip0, (uintptr_t) media_uqsub8);
                blr(ip0);
                mov(dst, r0);
                break;
            }
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
                auto dst = DSTREG();
                movx(r0, r29);
                MOVOP1(r1);
                MOVOP2(r2);
                movx(ip0, (uintptr_t) media_ssub8);
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
                auto dst = DSTREG();
                movx(r0, r29);
                MOVOP1(r1);
                MOVOP2(r2);
                movx(ip0, (uintptr_t) media_sel);
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
                movx(ip0, (uintptr_t) cpu_update_mode);
                blr(ip0);
                break;
            }
            case IR_EXCEPTION: {
                switch (inst.op1) {
                    case E_SWI:
                        movx(r0, r29);
                        mov(r1, (ArmInstr) {inst.op2}.sw_intr.arg);
                        movx(ip0, (uintptr_t) cpu->handle_svc);
                        blr(ip0);
                        break;
                    case E_UND:
                        movx(r0, r29);
                        mov(r1, inst.op2);
                        movx(ip0, (uintptr_t) cpu_undefined_fail);
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
                if (spdisp) subx(sp, sp, spdisp);

                movx(r29, (uintptr_t) cpu);
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
                if (spdisp) addx(sp, sp, spdisp);
                for (int i = (backend->hralloc.count[REG_SAVED] - 1) & ~1;
                     i >= 0; i -= 2) {
                    pop(Reg(SAVEDREGS_BASE + i), Reg(SAVEDREGS_BASE + i + 1));
                }
                pop(fp, lr);

                if (inst.opcode == IR_END_LINK) {
                    Label(nolink);
                    Label(linkaddr);
                    cmpx(r0, 0);
                    ble(nolink);
                    ldrlx(r16, linkaddr);
                    br(r16);
                    align(8);
                    L(linkaddr);
                    Label(linklabel);
                    backend->links[backend->nlinks++] =
                        (LinkPatch) {linklabel, inst.op1, inst.op2};
                    dword(linklabel);
                    L(nolink);
                }

                ret();
                break;
            }
            default:
                lerror("unimpl ir instr %d", inst.opcode);
                break;
        }
        STOREDST();
    }

    align(8);
    L(lld8);
    dword(Lnew(cpu->read8));
    L(lld16);
    dword(Lnew(cpu->read16));
    L(lld32);
    dword(Lnew(cpu->read32));
    L(lst8);
    dword(Lnew(cpu->write8));
    L(lst16);
    dword(Lnew(cpu->write16));
    L(lst32);
    dword(Lnew(cpu->write32));
    L(lldf32);
    dword(Lnew(cpu->readf32));
    L(lldf64);
    dword(Lnew(cpu->readf64));
    L(lstf32);
    dword(Lnew(cpu->writef32));
    L(lstf64);
    dword(Lnew(cpu->writef64));

    return backend;
}

void compileVFPDataProc(ArmCodeBackend* backend, ArmInstr instr) {
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
                ldrd(v0, CPU(d[vd]));
                ldrd(v1, CPU(d[vn]));
                ldrd(v2, CPU(d[vm]));
            } else {
                ldrs(v0, CPU(s[vd]));
                ldrs(v1, CPU(s[vn]));
                ldrs(v2, CPU(s[vm]));
            }
            fpdataproc3source(dp, 0, 0, cpopc, cpopc ^ op, v0, v1, v2, v0);
            if (dp) {
                strd(v0, CPU(d[vd]));
            } else {
                strs(v0, CPU(s[vd]));
            }
            break;
        case 2:
        case 3:
        case 8: {
            if (dp) {
                ldrd(v1, CPU(d[vn]));
                ldrd(v2, CPU(d[vm]));
            } else {
                ldrs(v1, CPU(s[vn]));
                ldrs(v2, CPU(s[vm]));
            }
            u32 opcode = cpopc == 8 ? 1 : cpopc == 2 ? op * 8 : 2 + op;
            fpdataproc2source(dp, 0, 0, opcode, v0, v1, v2);
            if (dp) {
                strd(v0, CPU(d[vd]));
            } else {
                strs(v0, CPU(s[vd]));
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
                        ldrd(v2, CPU(d[vm]));
                    } else {
                        ldrs(v2, CPU(s[vm]));
                    }
                    fpdataproc1source(dp, 0, 0, crn << 1 | op, v0, v2);
                    if (dp) {
                        strd(v0, CPU(d[vd]));
                    } else {
                        strs(v0, CPU(s[vd]));
                    }
                    break;
                case 4:
                case 5:
                    if (dp) {
                        ldrd(v0, CPU(d[vd]));
                        ldrd(v2, CPU(d[vm]));
                    } else {
                        ldrs(v0, CPU(s[vd]));
                        ldrs(v2, CPU(s[vm]));
                    }
                    fpcompare(dp, 0, 0, 0, (crn & 1) * 8, v0, v2);
                    mrs(r0, nzcv);
                    ldr(r1, CPU(fpscr));
                    ubfx(r0, r0, 28, 4);
                    bfi(r1, r0, 28, 4);
                    str(r1, CPU(fpscr));
                    break;
                case 7:
                    if (dp) {
                        vd = vd << 1 | ((instr.cp_data_proc.cpopc >> 2) & 1);
                        ldrd(v0, CPU(d[vm]));
                        fcvtsd(v0, v0);
                        strs(v0, CPU(s[vd]));
                    } else {
                        vd = vd >> 1;
                        ldrs(v0, CPU(s[vm]));
                        fcvtds(v0, v0);
                        strd(v0, CPU(d[vd]));
                    }
                    break;
                case 8:
                    if (dp) vm = vm << 1 | (instr.cp_data_proc.cp & 1);
                    ldr(r0, CPU(s[vm]));
                    fpconvertintvr(0, dp, 0, 0, 2 + !op, v0, r0);
                    if (dp) {
                        strd(v0, CPU(d[vd]));
                    } else {
                        strs(v0, CPU(s[vd]));
                    }
                    break;
                case 12:
                case 13:
                    if (dp) {
                        vd = vd << 1 | ((instr.cp_data_proc.cpopc >> 2) & 1);
                        ldrd(v0, CPU(d[vm]));
                    } else {
                        ldrs(v0, CPU(s[vm]));
                    }
                    fpconvertintrv(0, dp, 0, 3, !(crn & 1), r0, v0);
                    str(r0, CPU(s[vd]));
                    break;
            }
            break;
        }
    }
}

void compileVFPLoadMem(ArmCodeBackend* backend, ArmInstr instr, rasReg addr,
                       rasLabel lldf32, rasLabel lldf64) {
    u32 rcount;
    if (instr.cp_data_trans.p && !instr.cp_data_trans.w) {
        rcount = 1;
    } else {
        rcount = instr.cp_data_trans.offset;
        if (instr.cp_data_trans.cpnum & 1) rcount >>= 1;
    }

    u32 vd = instr.cp_data_trans.crd;

    if (rcount > 1) {
        push(r19, zr);
        mov(r19, addr);
        addr = r19;
    }

    if (instr.cp_data_trans.cpnum & 1) {
        for (int i = 0; i < rcount; i++) {
            movx(r0, r29);
            mov(r1, addr);
            ldrlx(ip0, lldf64);
            blr(ip0);
            strd(v0, CPU(d[(vd + i) & 15]));
            if (i < rcount - 1) add(addr, addr, 8);
        }
    } else {
        vd = vd << 1 | instr.cp_data_trans.n;

        for (int i = 0; i < rcount; i++) {
            movx(r0, r29);
            mov(r1, addr);
            ldrlx(ip0, lldf32);
            blr(ip0);
            strs(v0, CPU(s[(vd + i) & 31]));
            if (i < rcount - 1) add(addr, addr, 4);
        }
    }

    if (rcount > 1) {
        pop(r19, zr);
    }
}

void compileVFPStoreMem(ArmCodeBackend* backend, ArmInstr instr, rasReg addr,
                        rasLabel lstf32, rasLabel lstf64) {
    u32 rcount;
    if (instr.cp_data_trans.p && !instr.cp_data_trans.w) {
        rcount = 1;
    } else {
        rcount = instr.cp_data_trans.offset;
        if (instr.cp_data_trans.cpnum & 1) rcount >>= 1;
    }

    u32 vd = instr.cp_data_trans.crd;

    if (rcount > 1) {
        push(r19, zr);
        mov(r19, addr);
        addr = r19;
    }

    if (instr.cp_data_trans.cpnum & 1) {
        for (int i = 0; i < rcount; i++) {
            movx(r0, r29);
            mov(r1, addr);
            ldrd(v0, CPU(d[(vd + i) & 15]));
            ldrlx(ip0, lstf64);
            blr(ip0);
            if (i < rcount - 1) add(addr, addr, 8);
        }
    } else {
        vd = vd << 1 | instr.cp_data_trans.n;

        for (int i = 0; i < rcount; i++) {
            movx(r0, r29);
            mov(r1, addr);
            ldrs(v0, CPU(s[(vd + i) & 31]));
            ldrlx(ip0, lstf32);
            blr(ip0);
            if (i < rcount - 1) add(addr, addr, 4);
        }
    }

    if (rcount > 1) {
        pop(r19, zr);
    }
}

void compileVFPRead(ArmCodeBackend* backend, ArmInstr instr, rasReg dst) {
    if (instr.cp_reg_trans.cpopc == 7) {
        if (instr.cp_reg_trans.crn == 1) {
            ldr(dst, CPU(fpscr));
        } else {
            lwarn("unknown vfp special reg %d", instr.cp_reg_trans.crn);
            mov(dst, 0);
        }
        return;
    }

    u32 vn = instr.cp_reg_trans.crn << 1;
    if (instr.cp_reg_trans.cpnum & 1) vn |= instr.cp_reg_trans.cpopc & 1;
    else vn |= instr.cp_reg_trans.cp >> 2;

    ldr(dst, CPU(s[vn]));
}

void compileVFPWrite(ArmCodeBackend* backend, ArmInstr instr, rasReg src) {
    if (instr.cp_reg_trans.cpopc == 7) {
        if (instr.cp_reg_trans.crn == 1) {
            str(src, CPU(fpscr));
        } else {
            lwarn("unknown vfp special reg %d", instr.cp_reg_trans.crn);
        }
        return;
    }

    u32 vn = instr.cp_reg_trans.crn << 1;
    if (instr.cp_reg_trans.cpnum & 1) vn |= instr.cp_reg_trans.cpopc & 1;
    else vn |= instr.cp_reg_trans.cp >> 2;

    str(src, CPU(s[vn]));
}

void compileVFPRead64(ArmCodeBackend* backend, ArmInstr instr, rasReg dst,
                      bool h) {
    if (instr.cp_double_reg_trans.cpnum & 1) {
        u32 vm = instr.cp_double_reg_trans.crm;
        if (h) {
            ldr(dst, CPU(d[vm], 4));
        } else {
            ldr(dst, CPU(d[vm]));
        }
    } else {
        u32 vm = instr.cp_double_reg_trans.crm << 1 |
                 ((instr.cp_double_reg_trans.cp >> 1) & 1);
        if (h) {
            ldr(dst, CPU(s[vm + 1]));
        } else {
            ldr(dst, CPU(s[vm]));
        }
    }
}

void compileVFPWrite64(ArmCodeBackend* backend, ArmInstr instr, rasReg src,
                       bool h) {
    if (instr.cp_double_reg_trans.cpnum & 1) {
        u32 vm = instr.cp_double_reg_trans.crm;
        if (h) {
            str(src, CPU(d[vm], 4));
        } else {
            str(src, CPU(d[vm]));
        }
    } else {
        u32 vm = instr.cp_double_reg_trans.crm << 1 |
                 ((instr.cp_double_reg_trans.cp >> 1) & 1);
        if (h) {
            if (vm < 31) str(src, CPU(s[vm + 1]));
        } else {
            str(src, CPU(s[vm]));
        }
    }
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
    for (int i = 0; i < backend->nlinks; i++) {
        auto patch = backend->links[i];
        JITBlock* linkblock =
            get_jitblock(backend->cpu, patch.attrs, patch.addr);
        rasDefineLabelExternal(patch.lab, linkblock->code);
        Vec_push(linkblock->linkingblocks,
                 ((BlockLocation) {block->attrs, block->start_addr}));
    }   
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
