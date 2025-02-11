#ifdef __aarch64__

#include "backend_arm.h"

#include <capstone/capstone.h>
#include <vector>
#include <xbyak_aarch64/xbyak_aarch64.h>

using namespace Xbyak_aarch64;

struct LinkPatch {
    u32 jmp_offset;
    u32 attrs;
    u32 addr;
};

enum {
    CB_LOAD8,
    CB_LOAD16,
    CB_LOAD32,
    CB_STORE8,
    CB_STORE16,
    CB_STORE32,

    CB_LOADF32,
    CB_LOADF64,
    CB_STOREF32,
    CB_STOREF64,

    CB_MAX
};

struct Code : Xbyak_aarch64::CodeGenerator {
    RegAllocation* regalloc;
    HostRegAllocation hralloc;
    ArmCore* cpu;

    // x0-x2 scratch
    // x3-x15 temp
    // x16-x17 scratch
    // x19-x28 saved
    // x29 cpu ptr

    const int tempBase = 3;
    const int tempMax = 13;
    const int savedBase = 19;
    const int savedMax = 10;

    std::vector<LinkPatch> links;

    Label cblabels[CB_MAX] = {};
    bool usingcb[CB_MAX] = {};

    Code(IRBlock* ir, RegAllocation* regalloc, ArmCore* cpu);

    ~Code() {
        hostregalloc_free(&hralloc);
    }

    void print_hostregs() {
        printf("Host Regs:");
        for (u32 i = 0; i < hralloc.nregs; i++) {
            printf(" $%d:", i);
            int operand = getOpForReg(i);
            if (operand >= 32) {
                operand -= 32;
                printf("[sp, #0x%x]", 4 * operand);
            } else {
                printf("w%d", operand);
            }
        }
        printf("\n");
    }

    int getSPDisp() {
        return (hralloc.count[REG_STACK] * 4 + 15) & ~15;
    }

    // returns:
    // 0-31 : reg index
    // 32+n : stack index
    int getOpForReg(int i) {
        HostRegInfo hr = hralloc.hostreg_info[i];
        switch (hr.type) {
            case REG_TEMP:
                return tempBase + hr.index;
            case REG_SAVED:
                return savedBase + hr.index;
            case REG_STACK:
                return 32 + hr.index;
            default: // unreachable
                return 0;
        }
        // also unreachable
        return 0;
    }

    int getOp(int i) {
        int assn = regalloc->reg_assn[i];
        if (assn == -1) return -1;
        else return getOpForReg(assn);
    }

    void compileCBCall(int cb) {
        usingcb[cb] = true;
        ldr(x16, cblabels[cb]);
        blr(x16);
    }

    void placeCBLiterals() {
        u64 cbptrs[CB_MAX] = {
            (u64) cpu->read8,    (u64) cpu->read16,  (u64) cpu->read32,
            (u64) cpu->write8,   (u64) cpu->write16, (u64) cpu->write32,
            (u64) cpu->readf32,  (u64) cpu->readf64, (u64) cpu->writef32,
            (u64) cpu->writef64,
        };
        align(8);
        for (int i = 0; i < CB_MAX; i++) {
            if (usingcb[i]) {
                L(cblabels[i]);
                dd(cbptrs[i]);
                dd(cbptrs[i] >> 32);
            }
        }
    }

    void compileVFPDataProc(ArmInstr instr);
    void compileVFPLoadMem(ArmInstr instr, WReg addr);
    void compileVFPStoreMem(ArmInstr instr, WReg addr);
    void compileVFPRead(ArmInstr instr, WReg dst);
    void compileVFPWrite(ArmInstr instr, WReg src);
    void compileVFPRead64(ArmInstr instr, WReg dst, bool hi);
    void compileVFPWrite64(ArmInstr instr, WReg src, bool hi);
};

#define CPU(m, ...)                                                            \
    (ptr(x29, (u32) offsetof(ArmCore, m) __VA_OPT__(+) __VA_ARGS__))

#define LOADOP(i, flbk)                                                        \
    ({                                                                         \
        auto dst = flbk;                                                       \
        if (inst.imm##i) {                                                     \
            mov(dst, inst.op##i);                                              \
        } else {                                                               \
            int op = getOp(inst.op##i);                                        \
            if (op >= 32) {                                                    \
                op -= 32;                                                      \
                ldr(dst, ptr(sp, 4 * op));                                     \
            } else {                                                           \
                dst = WReg(op);                                                \
            }                                                                  \
        }                                                                      \
        dst;                                                                   \
    })
#define LOADOP1() LOADOP(1, w16)
#define LOADOP2() LOADOP(2, w17)

#define MOVOP(i, dst)                                                          \
    ({                                                                         \
        auto src = LOADOP(i, dst);                                             \
        if (src.getIdx() != dst.getIdx()) mov(dst, src);                       \
    })
#define MOVOP1(dst) MOVOP(1, dst)
#define MOVOP2(dst) MOVOP(2, dst)

#define DSTREG()                                                               \
    ({                                                                         \
        int op = getOp(i);                                                     \
        (op >= 32) ? w16 : WReg(op);                                           \
    })
#define STOREDST()                                                             \
    ({                                                                         \
        int op = getOp(i);                                                     \
        if (op >= 32) {                                                        \
            op -= 32;                                                          \
            str(w16, ptr(sp, 4 * op));                                         \
        }                                                                      \
    })

Code::Code(IRBlock* ir, RegAllocation* regalloc, ArmCore* cpu)
    : Xbyak_aarch64::CodeGenerator(4096, Xbyak_aarch64::AutoGrow),
      regalloc(regalloc), cpu(cpu) {

    hralloc = allocate_host_registers(regalloc, tempMax, savedMax);

    u32 flags_mask = 0; // mask for which flags to store
    u32 lastflags = 0;  // last var for which flags were set

    u32 jmptarget = -1;

    Label looplabel;

    std::vector<Label> labels;

    for (u32 i = 0; i < ir->code.size; i++) {
        while (i < ir->code.size && ir->code.d[i].opcode == IR_NOP) i++;
        if (i == ir->code.size) break;
        IRInstr inst = ir->code.d[i];
        if (i >= jmptarget && inst.opcode != IR_JELSE) {
            L(labels.back());
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
                    mov(w0, 0);
                    flags_mask = 0;
                }
                if (inst.imm2) {
                    if (inst.op2) orr(w0, w0, BIT(31 - inst.op1));
                } else {
                    auto src = LOADOP2();
                    bfi(w0, src, 31 - inst.op1, 1);
                }
                flags_mask |= BIT(31 - inst.op1);
                if (ir->code.d[i + 2].opcode != IR_STORE_FLAG) {
                    ldr(w1, CPU(cpsr));
                    and_imm(w1, w1, ~flags_mask, w2);
                    orr(w1, w1, w0);
                    str(w1, CPU(cpsr));
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
                ldr(w0, CPU(cpsr));
                bfi(w0, src, 5, 1);
                str(w0, CPU(cpsr));
                break;
            }
            case IR_VFP_DATA_PROC: {
                compileVFPDataProc(ArmInstr(inst.op1));
                break;
            }
            case IR_VFP_LOAD_MEM: {
                auto addr = LOADOP2();
                compileVFPLoadMem(ArmInstr(inst.op1), addr);
                break;
            }
            case IR_VFP_STORE_MEM: {
                auto addr = LOADOP2();
                compileVFPStoreMem(ArmInstr(inst.op1), addr);
                break;
            }
            case IR_VFP_READ: {
                auto dst = DSTREG();
                compileVFPRead(ArmInstr(inst.op1), dst);
                break;
            }
            case IR_VFP_WRITE: {
                auto src = LOADOP2();
                compileVFPWrite(ArmInstr(inst.op1), src);
                break;
            }
            case IR_VFP_READ64L: {
                auto dst = DSTREG();
                compileVFPRead64(ArmInstr(inst.op1), dst, false);
                break;
            }
            case IR_VFP_READ64H: {
                auto dst = DSTREG();
                compileVFPRead64(ArmInstr(inst.op1), dst, true);
                break;
            }
            case IR_VFP_WRITE64L: {
                auto src = LOADOP2();
                compileVFPWrite64(ArmInstr(inst.op1), src, false);
                break;
            }
            case IR_VFP_WRITE64H: {
                auto src = LOADOP2();
                compileVFPWrite64(ArmInstr(inst.op1), src, true);
                break;
            }
            case IR_CP15_READ: {
                auto dst = DSTREG();
                mov(x0, x29);
                mov(w1, inst.op1);
                mov(x16, (u64) cpu->cp15_read);
                blr(x16);
                mov(dst, w0);
                break;
            }
            case IR_CP15_WRITE: {
                auto src = LOADOP2();
                mov(x0, x29);
                mov(w1, inst.op1);
                mov(w2, src);
                mov(x16, (u64) cpu->cp15_write);
                blr(x16);
                break;
            }
            case IR_LOAD_MEM8: {
                auto dst = DSTREG();
                mov(x0, x29);
                MOVOP1(w1);
                mov(w2, 0);
                compileCBCall(CB_LOAD8);
                mov(dst, w0);
                break;
            }
            case IR_LOAD_MEMS8: {
                auto dst = DSTREG();
                mov(x0, x29);
                MOVOP1(w1);
                mov(w2, 1);
                compileCBCall(CB_LOAD8);
                mov(dst, w0);
                break;
            }
            case IR_LOAD_MEM16: {
                auto dst = DSTREG();
                mov(x0, x29);
                MOVOP1(w1);
                mov(w2, 0);
                compileCBCall(CB_LOAD16);
                mov(dst, w0);
                break;
            }
            case IR_LOAD_MEMS16: {
                auto dst = DSTREG();
                mov(x0, x29);
                MOVOP1(w1);
                mov(w2, 1);
                compileCBCall(CB_LOAD16);
                mov(dst, w0);
                break;
            }
            case IR_LOAD_MEM32: {
                auto dst = DSTREG();
                mov(x0, x29);
                MOVOP1(w1);
                compileCBCall(CB_LOAD32);
                mov(dst, w0);
                break;
            }
            case IR_STORE_MEM8: {
                mov(x0, x29);
                MOVOP1(w1);
                MOVOP2(w2);
                compileCBCall(CB_STORE8);
                break;
            }
            case IR_STORE_MEM16: {
                mov(x0, x29);
                MOVOP1(w1);
                MOVOP2(w2);
                compileCBCall(CB_STORE16);
                break;
            }
            case IR_STORE_MEM32: {
                mov(x0, x29);
                MOVOP1(w1);
                MOVOP2(w2);
                compileCBCall(CB_STORE32);
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
                    ands_imm(dst, src1, inst.op2, w17);
                } else if (inst.imm1) {
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    ands_imm(dst, src2, inst.op1, w16);
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
                    orr_imm(dst, src1, inst.op2, w17);
                } else if (inst.imm1) {
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    orr_imm(dst, src2, inst.op1, w16);
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
                    eor_imm(dst, src1, inst.op2, w17);
                } else if (inst.imm1) {
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    eor_imm(dst, src2, inst.op1, w16);
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
                    Label lelse, lendif;
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
                    Label lelse, lendif;
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
                    Label lelse, lendif;
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
                cset(w0, CS);
                extr(dst, w0, src, 1);
                break;
            }
            case IR_ADD: {
                if (inst.imm2) {
                    auto src1 = LOADOP1();
                    auto dst = DSTREG();
                    adds_imm(dst, src1, inst.op2, w17);
                } else if (inst.imm1) {
                    auto src2 = LOADOP2();
                    auto dst = DSTREG();
                    adds_imm(dst, src2, inst.op1, w16);
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
                    subs_imm(dst, src1, inst.op2, w17);
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
                        smull(XReg(dst.getIdx()), src1, src2);
                    } else {
                        umull(XReg(dst.getIdx()), src1, src2);
                    }
                    STOREDST();
                    i++;
                    auto dsth = DSTREG();
                    lsr(XReg(dsth.getIdx()), XReg(dst.getIdx()), 32);
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
                smull(XReg(dst.getIdx()), src1, src2);
                lsr(XReg(dst.getIdx()), XReg(dst.getIdx()), 32);
                break;
            }
            case IR_UMULH: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                umull(XReg(dst.getIdx()), src1, src2);
                lsr(XReg(dst.getIdx()), XReg(dst.getIdx()), 32);
                break;
            }
            case IR_SMULW: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                smull(XReg(dst.getIdx()), src1, src2);
                lsr(XReg(dst.getIdx()), XReg(dst.getIdx()), 16);
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
                csel(dst, wzr, src, LT);
                mov(w0, MASK(inst.op1));
                cmp(src, w0);
                csel(dst, w0, dst, GT);
                break;
            }
            case IR_MEDIA_UADD8: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                mov(x0, x29);
                mov(w1, src1);
                mov(w2, src2);
                mov(x16, (u64) media_uadd8);
                blr(x16);
                mov(dst, w0);
                break;
            }
            case IR_MEDIA_USUB8: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                mov(x0, x29);
                mov(w1, src1);
                mov(w2, src2);
                mov(x16, (u64) media_usub8);
                blr(x16);
                mov(dst, w0);
                break;
            }
            case IR_MEDIA_UQADD8: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                mov(v0.s[0], src1);
                mov(v1.s[0], src2);
                uqadd(v0.b8, v0.b8, v1.b8);
                mov(dst, v0.s[0]);
                break;
            }
            case IR_MEDIA_UQSUB8: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                mov(v0.s[0], src1);
                mov(v1.s[0], src2);
                uqsub(v0.b8, v0.b8, v1.b8);
                mov(dst, v0.s[0]);
                break;
            }
            case IR_MEDIA_UHADD8: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                mov(v0.s[0], src1);
                mov(v1.s[0], src2);
                uhadd(v0.b8, v0.b8, v1.b8);
                mov(dst, v0.s[0]);
                break;
            }
            case IR_MEDIA_SSUB8: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                mov(x0, x29);
                mov(w1, src1);
                mov(w2, src2);
                mov(x16, (u64) media_ssub8);
                blr(x16);
                mov(dst, w0);
                break;
            }
            case IR_MEDIA_QSUB8: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                mov(v0.s[0], src1);
                mov(v1.s[0], src2);
                sqsub(v0.b8, v0.b8, v1.b8);
                mov(dst, v0.s[0]);
                break;
            }
            case IR_MEDIA_SEL: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                mov(x0, x29);
                mov(w1, src1);
                mov(w2, src2);
                mov(x16, (u64) media_sel);
                blr(x16);
                mov(dst, w0);
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
                    cset(dst, MI);
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
                    cset(dst, EQ);
                }
                break;
            }
            case IR_GETC: {
                auto dst = DSTREG();
                cset(dst, CS);
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
                cset(w0, CS);
                tst(cond, cond);
                csel(dst, w0, src, EQ);
                lastflags = 0;
                break;
            }
            case IR_GETV: {
                auto dst = DSTREG();
                cset(dst, VS);
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
                labels.push_back(Label());
                auto cond = LOADOP1();
                cbz(cond, labels.back());
                break;
            }
            case IR_JNZ: {
                jmptarget = inst.op2;
                labels.push_back(Label());
                auto cond = LOADOP1();
                cbnz(cond, labels.back());
                break;
            }
            case IR_JELSE: {
                jmptarget = inst.op2;
                auto elselbl = labels.back();
                labels.push_back(Label());
                b(labels.back());
                L(elselbl);
                break;
            }
            case IR_MODESWITCH: {
                mov(x0, x29);
                mov(w1, inst.op1);
                mov(x16, (u64) cpu_update_mode);
                blr(x16);
                break;
            }
            case IR_EXCEPTION: {
                switch (inst.op1) {
                    case E_SWI:
                        mov(x0, x29);
                        mov(w1, (ArmInstr) {inst.op2}.sw_intr.arg);
                        mov(x16, (u64) cpu->handle_svc);
                        blr(x16);
                        break;
                    case E_UND:
                        mov(x0, x29);
                        mov(w1, inst.op2);
                        mov(x16, (u64) cpu_undefined_fail);
                        blr(x16);
                        break;
                }
                break;
            }
            case IR_WFE: {
                mov(w0, 1);
                strb(w0, CPU(wfe));
                break;
            }
            case IR_BEGIN: {

                stp(x29, x30, pre_ptr(sp, -0x10));
                for (int i = 0; i < hralloc.count[REG_SAVED]; i += 2) {
                    stp(XReg(savedBase + i), XReg(savedBase + i + 1),
                        pre_ptr(sp, -0x10));
                }
                int spdisp = getSPDisp();
                if (spdisp) sub(sp, sp, spdisp);

                mov(x29, (u64) cpu);
                L(looplabel);

                break;
            }
            case IR_END_RET:
            case IR_END_LINK:
            case IR_END_LOOP: {
                lastflags = 0;

                ldr(x0, CPU(cycles));
                sub(x0, x0, ir->numinstr);
                str(x0, CPU(cycles));

                if (inst.opcode == IR_END_LOOP) {
                    cmp(x0, 0);
                    bgt(looplabel);
                }

                int spdisp = getSPDisp();
                if (spdisp) add(sp, sp, spdisp);
                for (int i = (hralloc.count[REG_SAVED] - 1) & ~1; i >= 0;
                     i -= 2) {
                    ldp(XReg(savedBase + i), XReg(savedBase + i + 1),
                        post_ptr(sp, 0x10));
                }
                ldp(x29, x30, post_ptr(sp, 0x10));

                if (inst.opcode == IR_END_LINK) {
                    Label nolink, linkaddr;
                    cmp(x0, 0);
                    ble(nolink);
                    ldr(x16, linkaddr);
                    br(x16);
                    align(8);
                    L(linkaddr);
                    links.push_back((LinkPatch) {(u32) (getCurr() - getCode()),
                                                 inst.op1, inst.op2});
                    dd(0);
                    dd(0);
                    L(nolink);
                }

                ret();
                break;
            }
            default:
                break;
        }
        STOREDST();
    }

    placeCBLiterals();
}

#define LDSN() ldr(s0, CPU(s[vn]))
#define LDSM() ldr(s1, CPU(s[vm]))
#define LDSD() ldr(s2, CPU(s[vd]))
#define LDSNM()                                                                \
    LDSN();                                                                    \
    LDSM()
#define LDSDM()                                                                \
    LDSD();                                                                    \
    LDSM()
#define LDSNMD()                                                               \
    LDSNM();                                                                   \
    LDSD()
#define STSD() str(s0, CPU(s[vd]))

#define LDDN() ldr(d0, CPU(d[vn]))
#define LDDM() ldr(d1, CPU(d[vm]))
#define LDDD() ldr(d2, CPU(d[vd]))
#define LDDNM()                                                                \
    LDDN();                                                                    \
    LDDM()
#define LDDDM()                                                                \
    LDDD();                                                                    \
    LDDM()
#define LDDNMD()                                                               \
    LDDNM();                                                                   \
    LDDD()
#define STDD() str(d0, CPU(d[vd]))

void Code::compileVFPDataProc(ArmInstr instr) {
    bool dp = instr.cp_data_proc.cpnum & 1;
    u32 vd = instr.cp_data_proc.crd;
    u32 vn = instr.cp_data_proc.crn;
    u32 vm = instr.cp_data_proc.crm;
    if (!dp) {
        vd = vd << 1 | ((instr.cp_data_proc.cpopc >> 2) & 1);
        vn = vn << 1 | (instr.cp_data_proc.cp >> 2);
        vm = vm << 1 | (instr.cp_data_proc.cp & 1);
    }
    bool op = instr.cp_data_proc.cp & 2;

    switch (instr.cp_data_proc.cpopc & 0b1011) {
        case 0:
            if (op) {
                if (dp) {
                    LDDNMD();
                    fmsub(d0, d0, d1, d2);
                    STDD();
                } else {
                    LDSNMD();
                    fmsub(s0, s0, s1, s2);
                    STSD();
                }
            } else {
                if (dp) {
                    LDDNMD();
                    fmadd(d0, d0, d1, d2);
                    STDD();
                } else {
                    LDSNMD();
                    fmadd(s0, s0, s1, s2);
                    STSD();
                }
            }
            break;
        case 1:
            if (op) {
                if (dp) {
                    LDDNMD();
                    fnmadd(d0, d0, d1, d2);
                    STDD();
                } else {
                    LDSNMD();
                    fnmadd(s0, s0, s1, s2);
                    STSD();
                }
            } else {
                if (dp) {
                    LDDNMD();
                    fnmsub(d0, d0, d1, d2);
                    STDD();
                } else {
                    LDSNMD();
                    fnmsub(s0, s0, s1, s2);
                    STSD();
                }
            }
            break;
        case 2:
            if (dp) {
                LDDNM();
                if (op) fnmul(d0, d0, d1);
                else fmul(d0, d0, d1);
                STDD();
            } else {
                LDSNM();
                if (op) fnmul(s0, s0, s1);
                else fmul(s0, s0, s1);
                STSD();
            }
            break;
        case 3:
            if (op) {
                if (dp) {
                    LDDNM();
                    fsub(d0, d0, d1);
                    STDD();
                } else {
                    LDSNM();
                    fsub(s0, s0, s1);
                    STSD();
                }
            } else {
                if (dp) {
                    LDDNM();
                    fadd(d0, d0, d1);
                    STDD();
                } else {
                    LDSNM();
                    fadd(s0, s0, s1);
                    STSD();
                }
            }
            break;
        case 8:
            if (dp) {
                LDDNM();
                fdiv(d0, d0, d1);
                STDD();
            } else {
                LDSNM();
                fdiv(s0, s0, s1);
                STSD();
            }
            break;
        case 11: {
            op = instr.cp_data_proc.cp & 4;
            switch (instr.cp_data_proc.crn) {
                case 0:
                    if (op) {
                        if (dp) {
                            LDDM();
                            fabs(d0, d1);
                            STDD();
                        } else {
                            LDSM();
                            fabs(s0, s1);
                            STSD();
                        }
                    } else {
                        if (dp) {
                            LDDM();
                            fmov(d0, d1);
                            STDD();
                        } else {
                            LDSM();
                            fmov(s0, s1);
                            STSD();
                        }
                    }
                    break;
                case 1:
                    if (op) {
                        if (dp) {
                            LDDM();
                            fsqrt(d0, d1);
                            STDD();
                        } else {
                            LDSM();
                            fsqrt(s0, s1);
                            STSD();
                        }
                    } else {
                        if (dp) {
                            LDDM();
                            fneg(d0, d1);
                            STDD();
                        } else {
                            LDSM();
                            fneg(s0, s1);
                            STSD();
                        }
                    }
                    break;
                case 4:
                case 5:
                    if (dp) {
                        if (instr.cp_data_proc.crn & 1) {
                            LDDD();
                            fcmp(d2, 0);
                        } else {
                            LDDDM();
                            fcmp(d2, d1);
                        }
                    } else {
                        if (instr.cp_data_proc.crn & 1) {
                            LDSD();
                            fcmp(s2, 0);
                        } else {
                            LDSDM();
                            fcmp(s2, s1);
                        }
                    }
                    mrs(x0, 3, 3, 4, 2, 0); // mrs x0, nzcv
                    // arm32 flags should be the same as arm64 flags
                    ldr(w1, CPU(fpscr));
                    ubfx(w0, w0, 28, 4);
                    bfi(w1, w0, 28, 4);
                    str(w1, CPU(fpscr));
                    break;
                case 7:
                    if (dp) {
                        vd = vd << 1 | ((instr.cp_data_proc.cpopc >> 2) & 1);
                        LDDM();
                        fcvt(s0, d1);
                        STSD();
                    } else {
                        vd = vd >> 1;
                        LDSM();
                        fcvt(d0, s1);
                        STDD();
                    }
                    break;
                case 8:
                    if (dp) {
                        vm = vm << 1 | (instr.cp_data_proc.cp & 1);
                        
                        ldr(w0, CPU(s[vm]));
                        if (op) {
                            scvtf(d0, w0);
                        } else {
                            ucvtf(d0, w0);
                        }
                        STDD();
                    } else {
                        ldr(w0, CPU(s[vm]));
                        if (op) {
                            scvtf(s0, w0);
                        } else {
                            ucvtf(s0, w0);
                        }
                        STSD();
                    }
                    break;
                case 12:
                case 13:
                    // TODO: deal with rounding mode properly
                    if (dp) {
                        vd = vd << 1 | ((instr.cp_data_proc.cpopc >> 2) & 1);
                        LDDM();
                        if (instr.cp_data_proc.crn & 1) {
                            fcvtzs(w0, d1);
                        } else {
                            fcvtzu(w0, d1);
                        }
                    } else {
                        LDSM();
                        if (instr.cp_data_proc.crn & 1) {
                            fcvtzs(w0, s1);
                        } else {
                            fcvtzu(w0, s1);
                        }
                    }
                    str(w0, CPU(s[vd]));
                    break;
            }
            break;
        }
    }
}

void Code::compileVFPRead(ArmInstr instr, WReg dst) {
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

void Code::compileVFPWrite(ArmInstr instr, WReg src) {
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

void Code::compileVFPRead64(ArmInstr instr, WReg dst, bool hi) {
    if (instr.cp_double_reg_trans.cpnum & 1) {
        u32 vm = instr.cp_double_reg_trans.crm;
        if (hi) {
            ldr(dst, CPU(d[vm], 4));
        } else {
            ldr(dst, CPU(d[vm]));
        }
    } else {
        u32 vm = instr.cp_double_reg_trans.crm << 1 |
                 ((instr.cp_double_reg_trans.cp >> 1) & 1);
        if (hi) {
            ldr(dst, CPU(s[vm + 1]));
        } else {
            ldr(dst, CPU(s[vm]));
        }
    }
}

void Code::compileVFPWrite64(ArmInstr instr, WReg src, bool hi) {
    if (instr.cp_double_reg_trans.cpnum & 1) {
        u32 vm = instr.cp_double_reg_trans.crm;
        if (hi) {
            str(src, CPU(d[vm], 4));
        } else {
            str(src, CPU(d[vm]));
        }
    } else {
        u32 vm = instr.cp_double_reg_trans.crm << 1 |
                 ((instr.cp_double_reg_trans.cp >> 1) & 1);
        if (hi) {
            if (vm < 31) str(src, CPU(s[vm + 1]));
        } else {
            str(src, CPU(s[vm]));
        }
    }
}

void Code::compileVFPLoadMem(ArmInstr instr, WReg addr) {
    u32 rcount;
    if (instr.cp_data_trans.p && !instr.cp_data_trans.w) {
        rcount = 1;
    } else {
        rcount = instr.cp_data_trans.offset;
        if (instr.cp_data_trans.cpnum & 1) rcount >>= 1;
    }

    u32 vd = instr.cp_data_trans.crd;

    if (rcount > 1) {
        str(x19, pre_ptr(sp, -16));
        mov(w19, addr);
        addr = w19;
    }

    if (instr.cp_data_trans.cpnum & 1) {
        for (int i = 0; i < rcount; i++) {
            mov(x0, x29);
            mov(w1, addr);
            compileCBCall(CB_LOADF64);
            str(d0, CPU(d[(vd + i) & 15]));
            if (i < rcount - 1) add(addr, addr, 8);
        }
    } else {
        vd = vd << 1 | instr.cp_data_trans.n;

        for (int i = 0; i < rcount; i++) {
            mov(x0, x29);
            mov(w1, addr);
            compileCBCall(CB_LOADF32);
            str(s0, CPU(s[(vd + i) & 31]));
            if (i < rcount - 1) add(addr, addr, 4);
        }
    }

    if (rcount > 1) {
        ldr(x19, post_ptr(sp, 16));
    }
}

void Code::compileVFPStoreMem(ArmInstr instr, WReg addr) {
    u32 rcount;
    if (instr.cp_data_trans.p && !instr.cp_data_trans.w) {
        rcount = 1;
    } else {
        rcount = instr.cp_data_trans.offset;
        if (instr.cp_data_trans.cpnum & 1) rcount >>= 1;
    }

    u32 vd = instr.cp_data_trans.crd;

    if (rcount > 1) {
        str(x19, pre_ptr(sp, -16));
        mov(w19, addr);
        addr = w19;
    }

    if (instr.cp_data_trans.cpnum & 1) {
        for (int i = 0; i < rcount; i++) {
            mov(x0, x29);
            mov(w1, addr);
            ldr(d0, CPU(d[(vd + i) & 15]));
            compileCBCall(CB_STOREF64);
            if (i < rcount - 1) add(addr, addr, 8);
        }
    } else {
        vd = vd << 1 | instr.cp_data_trans.n;

        for (int i = 0; i < rcount; i++) {
            mov(x0, x29);
            mov(w1, addr);
            ldr(s0, CPU(s[(vd + i) & 31]));
            compileCBCall(CB_STOREF32);
            if (i < rcount - 1) add(addr, addr, 4);
        }
    }

    if (rcount > 1) {
        ldr(x19, post_ptr(sp, 16));
    }
}

extern "C" {

void* backend_arm_generate_code(IRBlock* ir, RegAllocation* regalloc,
                                ArmCore* cpu) {
    return new Code(ir, regalloc, cpu);
}

JITFunc backend_arm_get_code(void* backend) {
    return (JITFunc) ((Code*) backend)->getCode();
}

void backend_arm_patch_links(JITBlock* block) {
    Code* code = (Code*) block->backend;
    for (auto [offset, attrs, addr] : code->links) {
        char* linkaddr = (char*) code->getCode() + offset;
        JITBlock* linkblock = get_jitblock(code->cpu, attrs, addr);
        *(u64*) linkaddr = (u64) linkblock->code;
        Vec_push(linkblock->linkingblocks,
                 ((BlockLocation) {block->attrs, block->start_addr}));
    }

    code->ready();
}

void backend_arm_free(void* backend) {
    delete ((Code*) backend);
}

void backend_arm_disassemble(void* backend) {
    Code* code = (Code*) backend;
    code->print_hostregs();
    csh handle;
    cs_insn* insn;
    cs_open(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, &handle);
    size_t count =
        cs_disasm(handle, code->getCode(), code->getSize(), 0, 0, &insn);
    printf("--------- JIT Disassembly at %p ------------\n", code->getCode());
    for (size_t i = 0; i < count; i++) {
        printf("%04lx: %08x\t%s %s\n", insn[i].address, *(u32*) &insn[i].bytes,
               insn[i].mnemonic, insn[i].op_str);
    }
    cs_free(insn, count);
    cs_close(&handle);
}
}

#endif