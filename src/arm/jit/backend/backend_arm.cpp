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
};

#define CPU(m) (ptr(x29, (u32) offsetof(ArmCore, m)))

#define _LOADOP(i, flbk)                                                       \
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
#define LOADOP1() _LOADOP(1, w16)
#define LOADOP2() _LOADOP(2, w17)

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

enum {
    CLBK_LOAD8,
    CLBK_LOAD16,
    CLBK_LOAD32,
    CLBK_STORE8,
    CLBK_STORE16,
    CLBK_STORE32,

    CLBK_MAX
};

Code::Code(IRBlock* ir, RegAllocation* regalloc, ArmCore* cpu)
    : Xbyak_aarch64::CodeGenerator(4096, Xbyak_aarch64::AutoGrow),
      regalloc(regalloc), cpu(cpu) {

    hralloc = allocate_host_registers(regalloc, tempMax, savedMax);

    u32 flags_mask = 0; // mask for which flags to store
    u32 lastflags = 0;  // last var for which flags were set

    u32 jmptarget = -1;

    Label looplabel;

    Label clbks[CLBK_MAX] = {};
    bool usingclbk[CLBK_MAX] = {};
    u64 clbkptrs[CLBK_MAX] = {
        (u64) cpu->read8,  (u64) cpu->read16,  (u64) cpu->read32,
        (u64) cpu->write8, (u64) cpu->write16, (u64) cpu->write32,
    };

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
                auto src = LOADOP2();
                bfi(w0, src, 31 - inst.op1, 1);
                flags_mask |= BIT(31 - inst.op1);
                if (ir->code.d[i + 2].opcode != IR_STORE_FLAG) {
                    ldr(w1, CPU(cpsr));
                    and_(w1, w1, ~flags_mask);
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
                mov(x0, x29);
                mov(w1, inst.op1);
                mov(x16, (u64) exec_vfp_data_proc);
                blr(x16);
                break;
            }
            case IR_VFP_LOAD_MEM: {
                auto addr = LOADOP2();
                mov(x0, x29);
                mov(w1, inst.op1);
                mov(w2, addr);
                mov(x16, (u64) exec_vfp_load_mem);
                blr(x16);
                break;
            }
            case IR_VFP_STORE_MEM: {
                auto addr = LOADOP2();
                mov(x0, x29);
                mov(w1, inst.op1);
                mov(w2, addr);
                mov(x16, (u64) exec_vfp_store_mem);
                blr(x16);
                break;
            }
            case IR_VFP_READ: {
                auto dst = DSTREG();
                mov(x0, x29);
                mov(w1, inst.op1);
                mov(x16, (u64) exec_vfp_read);
                blr(x16);
                mov(dst, w0);
                break;
            }
            case IR_VFP_WRITE: {
                auto src = LOADOP2();
                mov(x0, x29);
                mov(w1, inst.op1);
                mov(w2, src);
                mov(x16, (u64) exec_vfp_write);
                blr(x16);
                break;
            }
            case IR_VFP_READ64L: {
                auto dst = DSTREG();
                mov(x0, x29);
                mov(w1, inst.op1);
                mov(x16, (u64) exec_vfp_read64);
                blr(x16);
                mov(dst, w0);
                STOREDST();
                i++;
                auto dsth = DSTREG();
                lsr(XReg(dsth.getIdx()), x0, 32);
                break;
            }
            case IR_VFP_READ64H:
                break;
            case IR_VFP_WRITE64L: {
                auto src = LOADOP2();
                mov(x0, x29);
                mov(w1, inst.op1);
                i++;
                inst = ir->code.d[i];
                auto srch = LOADOP2();
                mov(w2, src);
                bfi(x2, XReg(srch.getIdx()), 32, 32);
                mov(x16, (u64) exec_vfp_write64);
                blr(x16);
                break;
            }
            case IR_VFP_WRITE64H:
                break;
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
                auto addr = LOADOP1();
                auto dst = DSTREG();
                mov(x0, x29);
                mov(w1, addr);
                mov(w2, 0);
                usingclbk[CLBK_LOAD8] = true;
                ldr(x16, clbks[CLBK_LOAD8]);
                blr(x16);
                mov(dst, w0);
                break;
            }
            case IR_LOAD_MEMS8: {
                auto addr = LOADOP1();
                auto dst = DSTREG();
                mov(x0, x29);
                mov(w1, addr);
                mov(w2, 1);
                usingclbk[CLBK_LOAD8] = true;
                ldr(x16, clbks[CLBK_LOAD8]);
                blr(x16);
                mov(dst, w0);
                break;
            }
            case IR_LOAD_MEM16: {
                auto addr = LOADOP1();
                auto dst = DSTREG();
                mov(x0, x29);
                mov(w1, addr);
                mov(w2, 0);
                usingclbk[CLBK_LOAD16] = true;
                ldr(x16, clbks[CLBK_LOAD16]);
                blr(x16);
                mov(dst, w0);
                break;
            }
            case IR_LOAD_MEMS16: {
                auto addr = LOADOP1();
                auto dst = DSTREG();
                mov(x0, x29);
                mov(w1, addr);
                mov(w2, 1);
                usingclbk[CLBK_LOAD16] = true;
                ldr(x16, clbks[CLBK_LOAD16]);
                blr(x16);
                mov(dst, w0);
                break;
            }
            case IR_LOAD_MEM32: {
                auto addr = LOADOP1();
                auto dst = DSTREG();
                mov(x0, x29);
                mov(w1, addr);
                mov(w2, 0);
                usingclbk[CLBK_LOAD32] = true;
                ldr(x16, clbks[CLBK_LOAD32]);
                blr(x16);
                mov(dst, w0);
                break;
            }
            case IR_STORE_MEM8: {
                auto addr = LOADOP1();
                auto data = LOADOP2();
                mov(x0, x29);
                mov(w1, addr);
                mov(w2, data);
                usingclbk[CLBK_STORE8] = true;
                ldr(x16, clbks[CLBK_STORE8]);
                blr(x16);
                break;
            }
            case IR_STORE_MEM16: {
                auto addr = LOADOP1();
                auto data = LOADOP2();
                mov(x0, x29);
                mov(w1, addr);
                mov(w2, data);
                usingclbk[CLBK_STORE16] = true;
                ldr(x16, clbks[CLBK_STORE16]);
                blr(x16);
                break;
            }
            case IR_STORE_MEM32: {
                auto addr = LOADOP1();
                auto data = LOADOP2();
                mov(x0, x29);
                mov(w1, addr);
                mov(w2, data);
                usingclbk[CLBK_STORE32] = true;
                ldr(x16, clbks[CLBK_STORE32]);
                blr(x16);
                break;
            }
            case IR_MOV: {
                auto src = LOADOP2();
                auto dst = DSTREG();
                mov(dst, src);
                break;
            }
            case IR_AND: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                ands(dst, src1, src2);
                lastflags = i;
                break;
            }
            case IR_OR: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                orr(dst, src1, src2);
                break;
            }
            case IR_XOR: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                eor(dst, src1, src2);
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
                    b(HS, lelse);
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
                    b(HS, lelse);
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
                    b(HS, lelse);
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
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                adds(dst, src1, src2);
                lastflags = i;
                break;
            }
            case IR_SUB: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                subs(dst, src1, src2);
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
                mov(w0, 0);
                cmp(src, w0);
                csel(dst, w0, src, LT);
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
            case IR_MEDIA_UQSUB8: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                mov(x0, x29);
                mov(w1, src1);
                mov(w2, src2);
                mov(x16, (u64) media_uqsub8);
                blr(x16);
                mov(dst, w0);
                break;
            }
            case IR_MEDIA_QSUB8: {
                auto src1 = LOADOP1();
                auto src2 = LOADOP2();
                auto dst = DSTREG();
                mov(x0, x29);
                mov(w1, src1);
                mov(w2, src2);
                mov(x16, (u64) media_qsub8);
                blr(x16);
                mov(dst, w0);
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

    for (int i = 0; i < CLBK_MAX; i++) {
        if (usingclbk[i]) {
            L(clbks[i]);
            dd(clbkptrs[i]);
            dd(clbkptrs[i] >> 32);
        }
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
        printf("%04lx: %s %s\n", insn[i].address, insn[i].mnemonic,
               insn[i].op_str);
    }
    cs_free(insn, count);
}
}

#endif