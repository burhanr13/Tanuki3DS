#ifdef __x86_64__

#include "backend_x86.h"

#ifndef NOCAPSTONE
#include <capstone/capstone.h>
#endif
#include <utility>
#include <vector>
#include <xbyak/xbyak.h>
#include <xbyak/xbyak_util.h>

struct LinkPatch {
    u32 jmp_offset;
    u32 attrs;
    u32 addr;
};

struct Code : Xbyak::CodeGenerator {
    RegAllocation* regalloc;
    HostRegAllocation hralloc;
    ArmCore* cpu;

#ifdef _WIN32
    Xbyak::Reg64 arg1 = rcx;
    Xbyak::Reg32 arg2 = edx;
    Xbyak::Reg32 arg3 = r8d;
    Xbyak::Xmm arg3f = xmm2;
#else
    Xbyak::Reg64 arg1 = rdi;
    Xbyak::Reg32 arg2 = esi;
    Xbyak::Reg32 arg3 = edx;
    Xbyak::Xmm arg3f = xmm0;
#endif
    std::vector<Xbyak::Reg32> tempregs =
#ifdef _WIN32
        {r9d, r10d, r11d};
#else
        {esi, edi, r8d, r9d, r10d, r11d};
#endif
    std::vector<Xbyak::Reg32> savedregs =
#ifdef _WIN32
        {ebp, edi, esi, r12d, r13d, r14d, r15d};
#else
        {ebp, r12d, r13d, r14d, r15d};
#endif
    std::vector<Xbyak::Address> stackslots;

    std::vector<LinkPatch> links;

    Code(IRBlock* ir, RegAllocation* regalloc, ArmCore* cpu);

    ~Code() {
        hostregalloc_free(&hralloc);
    }

    void print_hostregs() {
        printf("Host Regs:");
        for (u32 i = 0; i < hralloc.nregs; i++) {
            printf(" $%d:", i);
            auto& operand = _getOp(i);
            if (operand.isREG()) printf("%s", operand.toString());
            else printf("[rsp+%d]", 4 * hralloc.hostreg_info[i].index);
        }
        printf("\n");
    }

    int getSPDisp() {
        int disp =
            (hralloc.count[REG_SAVED] + 2) * 8 + hralloc.count[REG_STACK] * 4;
        disp = (disp + 15) & ~15;
        disp -= (hralloc.count[REG_SAVED] + 2) * 8;
        return disp;
    }

    const Xbyak::Operand& _getOp(int i) {
        HostRegInfo hr = hralloc.hostreg_info[i];
        switch (hr.type) {
            case REG_TEMP:
                return tempregs[hr.index];
            case REG_SAVED:
                return savedregs[hr.index];
            case REG_STACK:
                return stackslots[hr.index];
            default:
                std::unreachable();
        }
    }

    const Xbyak::Operand& getOp(int i) {
        return _getOp(regalloc->reg_assn[i]);
    }

    // older cpus don't support lzcnt
    // and will silently execute a bsr instead
    // very annoying
    void lzcnt_portable(const Xbyak::Reg dst, const Xbyak::Operand& src) {
        Xbyak::util::Cpu cpu;
        if (cpu.has(Xbyak::util::Cpu::tLZCNT)) {
            lzcnt(dst, src);
        } else {
            bsr(dst, src);
            mov(ecx, -1);
            cmove(dst, ecx);
            neg(dst);
            add(dst, 31);
        }
    }

    void compileVFPDataProc(ArmInstr instr);
    void compileVFPLoadMem(ArmInstr instr, const Xbyak::Operand& addr);
    void compileVFPStoreMem(ArmInstr instr, const Xbyak::Operand& addr);
    void compileVFPRead(ArmInstr instr, const Xbyak::Operand& dst);
    void compileVFPWrite(ArmInstr instr, const Xbyak::Operand& src);
    void compileVFPRead64(ArmInstr instr, const Xbyak::Operand& dst, bool hi);
    void compileVFPWrite64(ArmInstr instr, const Xbyak::Operand& src, bool hi);
};

#define CPU(m) (rbx + offsetof(ArmCore, m))

#define OP(op, dest, src)                                                      \
    ({                                                                         \
        if ((src).isMEM() && (dest).isMEM()) {                                 \
            mov(edx, src);                                                     \
            op(dest, edx);                                                     \
        } else op(dest, src);                                                  \
    })

#define LOAD(addr)                                                             \
    ({                                                                         \
        auto& dest = getOp(i);                                                 \
        OP(mov, dest, ptr[addr]);                                              \
    })

#define STORE(addr)                                                            \
    ({                                                                         \
        if (inst.imm2) {                                                       \
            mov(dword[addr], inst.op2);                                        \
        } else {                                                               \
            auto& src = getOp(inst.op2);                                       \
            OP(mov, ptr[addr], src);                                           \
        }                                                                      \
    })

#define SAMEREG(v1, v2) (regalloc->reg_assn[v1] == regalloc->reg_assn[v2])

#define BINARY(op)                                                             \
    ({                                                                         \
        bool op2eax = false;                                                   \
        if (!inst.imm2 && SAMEREG(inst.op2, i)) {                              \
            mov(eax, getOp(inst.op2));                                         \
            op2eax = true;                                                     \
        }                                                                      \
        auto& dest = getOp(i);                                                 \
        if (inst.imm1) {                                                       \
            mov(dest, inst.op1);                                               \
        } else if (!SAMEREG(inst.op1, i)) {                                    \
            OP(mov, dest, getOp(inst.op1));                                    \
        }                                                                      \
        if (op2eax) {                                                          \
            op(dest, eax);                                                     \
        } else if (inst.imm2) {                                                \
            op(dest, inst.op2);                                                \
        } else {                                                               \
            OP(op, dest, getOp(inst.op2));                                     \
        }                                                                      \
    })

Code::Code(IRBlock* ir, RegAllocation* regalloc, ArmCore* cpu)
    : Xbyak::CodeGenerator(4096, Xbyak::AutoGrow), regalloc(regalloc),
      cpu(cpu) {

    hralloc =
        allocate_host_registers(regalloc, tempregs.size(), savedregs.size());
    for (u32 i = 0; i < hralloc.count[REG_STACK]; i++) {
        stackslots.push_back(dword[rsp + 4 * i]);
    }

    u32 flags_mask = 0;
    u32 lastflags = 0;
    u32 nlabels = 0;
    u32 jmptarget = -1;

    for (u32 i = 0; i < ir->code.size; i++) {
        while (i < ir->code.size && ir->code.d[i].opcode == IR_NOP) i++;
        if (i == ir->code.size) break;
        IRInstr inst = ir->code.d[i];
        if (i >= jmptarget && inst.opcode != IR_JELSE) {
            L(std::to_string(nlabels++));
            jmptarget = -1;
        }
        if (!(inst.opcode == IR_NOP || inst.opcode == IR_STORE_FLAG ||
              inst.opcode == IR_GETC || inst.opcode == IR_GETV ||
              inst.opcode == IR_GETN || inst.opcode == IR_GETZ))
            lastflags = 0;
        switch (inst.opcode) {
            case IR_LOAD_REG:
                LOAD(CPU(r[inst.op1]));
                break;
            case IR_STORE_REG:
                STORE(CPU(r[inst.op1]));
                break;
            case IR_LOAD_REG_USR: {
                int rd = inst.op1;
                if (rd < 13) {
                    LOAD(CPU(banked_r8_12[0][rd - 8]));
                } else if (rd == 13) {
                    LOAD(CPU(banked_sp[0]));
                } else if (rd == 14) {
                    LOAD(CPU(banked_lr[0]));
                }
                break;
            }
            case IR_STORE_REG_USR: {
                int rd = inst.op1;
                if (rd < 13) {
                    STORE(CPU(banked_r8_12[0][rd - 8]));
                } else if (rd == 13) {
                    STORE(CPU(banked_sp[0]));
                } else if (rd == 14) {
                    STORE(CPU(banked_lr[0]));
                }
                break;
            }
            case IR_LOAD_FLAG: {
                LOAD(CPU(cpsr));
                auto& dest = getOp(i);
                shr(dest, 31 - inst.op1);
                and_(dest, 1);
                break;
            }
            case IR_STORE_FLAG: {
                if (ir->code.d[i - 2].opcode != IR_STORE_FLAG) {
                    xor_(ecx, ecx);
                    flags_mask = 0;
                }
                if (inst.imm2) {
                    if (inst.op2) or_(ecx, BIT(31 - inst.op1));
                } else {
                    mov(edx, getOp(inst.op2));
                    shl(edx, 31 - inst.op1);
                    or_(ecx, edx);
                }
                flags_mask |= BIT(31 - inst.op1);
                if (ir->code.d[i + 2].opcode != IR_STORE_FLAG) {
                    and_(dword[CPU(cpsr)], ~flags_mask);
                    or_(dword[CPU(cpsr)], ecx);
                }
                break;
            }
            case IR_LOAD_CPSR:
                LOAD(CPU(cpsr));
                break;
            case IR_STORE_CPSR:
                STORE(CPU(cpsr));
                break;
            case IR_LOAD_SPSR:
                LOAD(CPU(spsr));
                break;
            case IR_STORE_SPSR:
                STORE(CPU(spsr));
                break;
            case IR_LOAD_THUMB: {
                LOAD(CPU(cpsr));
                auto& dest = getOp(i);
                shr(dest, 5);
                and_(dest, 1);
                break;
            }
            case IR_STORE_THUMB: {
                and_(dword[CPU(cpsr)], ~BIT(5));
                if (inst.imm2) {
                    if (inst.op2) or_(dword[CPU(cpsr)], BIT(5));
                } else {
                    mov(edx, getOp(inst.op2));
                    shl(edx, 5);
                    or_(dword[CPU(cpsr)], edx);
                }
                break;
            }
            case IR_VFP_DATA_PROC: {
                compileVFPDataProc(ArmInstr(inst.op1));
                break;
            }
            case IR_VFP_LOAD_MEM: {
                if (inst.imm2) {
                    mov(edx, inst.op2);
                    compileVFPLoadMem(ArmInstr(inst.op1), edx);
                } else {
                    compileVFPLoadMem(ArmInstr(inst.op1), getOp(inst.op2));
                }
                break;
            }
            case IR_VFP_STORE_MEM: {
                if (inst.imm2) {
                    mov(edx, inst.op2);
                    compileVFPStoreMem(ArmInstr(inst.op1), edx);
                } else {
                    compileVFPStoreMem(ArmInstr(inst.op1), getOp(inst.op2));
                }
                break;
            }
            case IR_VFP_READ: {
                compileVFPRead(ArmInstr(inst.op1), getOp(i));
                break;
            }
            case IR_VFP_WRITE: {
                if (inst.imm2) {
                    mov(edx, inst.op2);
                    compileVFPWrite(ArmInstr(inst.op1), edx);
                } else {
                    compileVFPWrite(ArmInstr(inst.op1), getOp(inst.op2));
                }
                break;
            }
            case IR_VFP_READ64L: {
                compileVFPRead64(ArmInstr(inst.op1), getOp(i), false);
                break;
            }
            case IR_VFP_READ64H:
                compileVFPRead64(ArmInstr(inst.op1), getOp(i), true);
                break;
            case IR_VFP_WRITE64L: {
                if (inst.imm2) {
                    mov(edx, inst.op2);
                    compileVFPWrite64(ArmInstr(inst.op1), edx, false);
                } else {
                    compileVFPWrite64(ArmInstr(inst.op1), getOp(inst.op2),
                                      false);
                }
                break;
            }
            case IR_VFP_WRITE64H:
                if (inst.imm2) {
                    mov(edx, inst.op2);
                    compileVFPWrite64(ArmInstr(inst.op1), edx, true);
                } else {
                    compileVFPWrite64(ArmInstr(inst.op1), getOp(inst.op2),
                                      true);
                }
                break;
                break;
            case IR_CP15_READ: {
                mov(arg1, rbx);
                mov(arg2, inst.op1);
                mov(rax, (u64) cpu->cp15_read);
                call(rax);
                mov(getOp(i), eax);
                break;
            }
            case IR_CP15_WRITE: {
                if (inst.imm2) {
                    mov(arg3, inst.op2);
                } else {
                    mov(arg3, getOp(inst.op2));
                }
                mov(arg1, rbx);
                mov(arg2, inst.op1);
                mov(rax, (u64) cpu->cp15_write);
                call(rax);
                break;
            }
#ifdef JIT_FASTMEM
            case IR_LOAD_MEM8: {
                mov(rax, (u64) cpu->fastmem);
                if (inst.imm1) {
                    mov(edx, inst.op1);
                } else {
                    mov(edx, getOp(inst.op1));
                }
                movzx(eax, byte[rax + rdx]);
                mov(getOp(i), eax);
                break;
            }
            case IR_LOAD_MEMS8: {
                mov(rax, (u64) cpu->fastmem);
                if (inst.imm1) {
                    mov(edx, inst.op1);
                } else {
                    mov(edx, getOp(inst.op1));
                }
                movsx(eax, byte[rax + rdx]);
                mov(getOp(i), eax);
                break;
            }
            case IR_LOAD_MEM16: {
                mov(rax, (u64) cpu->fastmem);
                if (inst.imm1) {
                    mov(edx, inst.op1);
                } else {
                    mov(edx, getOp(inst.op1));
                }
                movzx(eax, word[rax + rdx]);
                mov(getOp(i), eax);
                break;
            }
            case IR_LOAD_MEMS16: {
                mov(rax, (u64) cpu->fastmem);
                if (inst.imm1) {
                    mov(edx, inst.op1);
                } else {
                    mov(edx, getOp(inst.op1));
                }
                movsx(eax, word[rax + rdx]);
                mov(getOp(i), eax);
                break;
            }
            case IR_LOAD_MEM32: {
                mov(rax, (u64) cpu->fastmem);
                if (inst.imm1) {
                    mov(edx, inst.op1);
                } else {
                    mov(edx, getOp(inst.op1));
                }
                mov(eax, dword[rax + rdx]);
                mov(getOp(i), eax);
                break;
            }
            case IR_STORE_MEM8: {
                mov(rax, (u64) cpu->fastmem);
                if (inst.imm1) {
                    mov(edx, inst.op1);
                } else {
                    mov(edx, getOp(inst.op1));
                }
                if (inst.imm2) {
                    mov(ecx, inst.op2);
                } else {
                    mov(ecx, getOp(inst.op2));
                }
                mov(byte[rax + rdx], cl);
                break;
            }
            case IR_STORE_MEM16: {
                mov(rax, (u64) cpu->fastmem);
                if (inst.imm1) {
                    mov(edx, inst.op1);
                } else {
                    mov(edx, getOp(inst.op1));
                }
                if (inst.imm2) {
                    mov(ecx, inst.op2);
                } else {
                    mov(ecx, getOp(inst.op2));
                }
                mov(word[rax + rdx], cx);
                break;
            }
            case IR_STORE_MEM32: {
                mov(rax, (u64) cpu->fastmem);
                if (inst.imm1) {
                    mov(edx, inst.op1);
                } else {
                    mov(edx, getOp(inst.op1));
                }
                if (inst.imm2) {
                    mov(ecx, inst.op2);
                } else {
                    mov(ecx, getOp(inst.op2));
                }
                mov(dword[rax + rdx], ecx);
                break;
            }
#else
            case IR_LOAD_MEM8: {
                xor_(arg3, arg3);
                if (inst.imm1) {
                    mov(arg2, inst.op1);
                } else {
                    auto& src = getOp(inst.op1);
                    if (src != arg2) mov(arg2, src);
                }
                mov(arg1, rbx);
                mov(rax, (u64) cpu->read8);
                call(rax);
                mov(getOp(i), eax);
                break;
            }
            case IR_LOAD_MEMS8: {
                mov(arg3, 1);
                if (inst.imm1) {
                    mov(arg2, inst.op1);
                } else {
                    auto& src = getOp(inst.op1);
                    if (src != arg2) mov(arg2, src);
                }
                mov(arg1, rbx);
                mov(rax, (u64) cpu->read8);
                call(rax);
                mov(getOp(i), eax);
                break;
            }
            case IR_LOAD_MEM16: {
                xor_(arg3, arg3);
                if (inst.imm1) {
                    mov(arg2, inst.op1);
                } else {
                    auto& src = getOp(inst.op1);
                    if (src != arg2) mov(arg2, src);
                }
                mov(arg1, rbx);
                mov(rax, (u64) cpu->read16);
                call(rax);
                mov(getOp(i), eax);
                break;
            }
            case IR_LOAD_MEMS16: {
                mov(arg3, 1);
                if (inst.imm1) {
                    mov(arg2, inst.op1);
                } else {
                    auto& src = getOp(inst.op1);
                    if (src != arg2) mov(arg2, src);
                }
                mov(arg1, rbx);
                mov(rax, (u64) cpu->read16);
                call(rax);
                mov(getOp(i), eax);
                break;
            }
            case IR_LOAD_MEM32: {
                if (inst.imm1) {
                    mov(arg2, inst.op1);
                } else {
                    auto& src = getOp(inst.op1);
                    if (src != arg2) mov(arg2, src);
                }
                mov(arg1, rbx);
                mov(rax, (u64) cpu->read32);
                call(rax);
                mov(getOp(i), eax);
                break;
            }
            case IR_STORE_MEM8: {
                if (inst.imm2) {
                    mov(arg3, inst.op2);
                } else {
                    mov(arg3, getOp(inst.op2));
                }
                if (inst.imm1) {
                    mov(arg2, inst.op1);
                } else {
                    auto& src = getOp(inst.op1);
                    if (src != arg2) mov(arg2, src);
                }
                mov(arg1, rbx);
                mov(rax, (u64) cpu->write8);
                call(rax);
                break;
            }
            case IR_STORE_MEM16: {
                if (inst.imm2) {
                    mov(arg3, inst.op2);
                } else {
                    mov(arg3, getOp(inst.op2));
                }
                if (inst.imm1) {
                    mov(arg2, inst.op1);
                } else {
                    auto& src = getOp(inst.op1);
                    if (src != arg2) mov(arg2, src);
                }
                mov(arg1, rbx);
                mov(rax, (u64) cpu->write16);
                call(rax);
                break;
            }
            case IR_STORE_MEM32: {
                if (inst.imm2) {
                    mov(arg3, inst.op2);
                } else {
                    mov(arg3, getOp(inst.op2));
                }
                if (inst.imm1) {
                    mov(arg2, inst.op1);
                } else {
                    auto& src = getOp(inst.op1);
                    if (src != arg2) mov(arg2, src);
                }
                mov(arg1, rbx);
                mov(rax, (u64) cpu->write32);
                call(rax);
                break;
            }
#endif
            case IR_MOV: {
                auto& dst = getOp(i);
                if (inst.imm2) {
                    mov(dst, inst.op2);
                } else if (!SAMEREG(inst.op2, i)) {
                    OP(mov, dst, getOp(inst.op2));
                }
                break;
            }
            case IR_AND:
                BINARY(and_);
                break;
            case IR_OR:
                BINARY(or_);
                break;
            case IR_XOR:
                BINARY(xor_);
                break;
            case IR_NOT: {
                auto& dst = getOp(i);
                if (inst.imm2) {
                    mov(dst, inst.op2);
                } else if (!SAMEREG(inst.op2, i)) {
                    OP(mov, dst, getOp(inst.op2));
                }
                not_(dst);
                break;
            }
            case IR_LSL: {
                bool op2eax = false;
                if (!inst.imm2 && SAMEREG(inst.op2, i)) {
                    mov(eax, getOp(inst.op2));
                    op2eax = true;
                }
                auto& dest = getOp(i);
                if (inst.imm1) {
                    mov(dest, inst.op1);
                } else if (!SAMEREG(inst.op1, i)) {
                    OP(mov, dest, getOp(inst.op1));
                }
                if (inst.imm2) {
                    if (inst.op2 >= 32) {
                        mov(dest, 0);
                    } else {
                        shl(dest, inst.op2);
                    }
                } else {
                    if (op2eax) {
                        mov(ecx, eax);
                    } else {
                        mov(ecx, getOp(inst.op2));
                    }
                    cmp(cl, 32);
                    inLocalLabel();
                    jb(".normal");
                    mov(dest, 0);
                    jmp(".end");
                    L(".normal");
                    shl(dest, cl);
                    L(".end");
                    outLocalLabel();
                }
                break;
            }
            case IR_LSR: {
                bool op2eax = false;
                if (!inst.imm2 && SAMEREG(inst.op2, i)) {
                    mov(eax, getOp(inst.op2));
                    op2eax = true;
                }
                auto& dest = getOp(i);
                if (inst.imm1) {
                    mov(dest, inst.op1);
                } else if (!SAMEREG(inst.op1, i)) {
                    OP(mov, dest, getOp(inst.op1));
                }
                if (inst.imm2) {
                    if (inst.op2 >= 32) {
                        mov(dest, 0);
                    } else {
                        shr(dest, inst.op2);
                    }
                } else {
                    if (op2eax) {
                        mov(ecx, eax);
                    } else {
                        mov(ecx, getOp(inst.op2));
                    }
                    cmp(cl, 32);
                    inLocalLabel();
                    jb(".normal");
                    mov(dest, 0);
                    jmp(".end");
                    L(".normal");
                    shr(dest, cl);
                    L(".end");
                    outLocalLabel();
                }
                break;
            }
            case IR_ASR: {
                bool op2eax = false;
                if (!inst.imm2 && SAMEREG(inst.op2, i)) {
                    mov(eax, getOp(inst.op2));
                    op2eax = true;
                }
                auto& dest = getOp(i);
                if (inst.imm1) {
                    mov(dest, inst.op1);
                } else if (!SAMEREG(inst.op1, i)) {
                    OP(mov, dest, getOp(inst.op1));
                }
                if (inst.imm2) {
                    if (inst.op2 >= 32) {
                        sar(dest, 31);
                    } else {
                        sar(dest, inst.op2);
                    }
                } else {
                    if (op2eax) {
                        mov(ecx, eax);
                    } else {
                        mov(ecx, getOp(inst.op2));
                    }
                    cmp(cl, 32);
                    inLocalLabel();
                    jb(".normal");
                    sar(dest, 31);
                    jmp(".end");
                    L(".normal");
                    sar(dest, cl);
                    L(".end");
                    outLocalLabel();
                }
                break;
            }
            case IR_ROR: {
                bool op2eax = false;
                if (!inst.imm2 && SAMEREG(inst.op2, i)) {
                    mov(eax, getOp(inst.op2));
                    op2eax = true;
                }
                auto& dest = getOp(i);
                if (inst.imm1) {
                    mov(dest, inst.op1);
                } else if (!SAMEREG(inst.op1, i)) {
                    OP(mov, dest, getOp(inst.op1));
                }
                if (inst.imm2) {
                    ror(dest, inst.op2);
                } else {
                    if (op2eax) {
                        mov(ecx, eax);
                    } else {
                        mov(ecx, getOp(inst.op2));
                    }
                    ror(dest, cl);
                }
                break;
            }
            case IR_RRC: {
                auto& dst = getOp(i);
                if (inst.imm1) {
                    mov(dst, inst.op1);
                } else if (!SAMEREG(inst.op1, i)) {
                    OP(mov, dst, getOp(inst.op1));
                }
                rcr(dst, 1);
                break;
            }
            case IR_ADD:
                BINARY(add);
                break;
            case IR_SUB:
                BINARY(sub);
                if (ir->code.d[i + 1].opcode == IR_GETC) cmc();
                break;
            case IR_ADC:
                BINARY(adc);
                break;
            case IR_SBC:
                cmc();
                BINARY(sbb);
                if (ir->code.d[i + 1].opcode == IR_GETC) cmc();
                break;
            case IR_MUL: {
                IRInstr hinst = ir->code.d[i + 1];
                if (hinst.opcode == IR_SMULH || hinst.opcode == IR_UMULH) {
                    if (inst.imm1) {
                        mov(eax, inst.op1);
                    } else {
                        mov(eax, getOp(inst.op1));
                    }
                    auto& msrc = inst.imm2 ? edx : getOp(inst.op2);
                    if (inst.imm2) mov(edx, inst.op2);
                    if (hinst.opcode == IR_SMULH) {
                        imul(msrc);
                    } else {
                        mul(msrc);
                    }
                    mov(getOp(i), eax);
                    mov(getOp(i + 1), edx);
                    i++;
                } else {
                    auto& dest = getOp(i);
                    if (inst.imm2) {
                        if (inst.imm1) {
                            mov(dest, inst.op1 * inst.op2);
                        } else {
                            auto& mdst = dest.isMEM() ? edx : dest;
                            imul(mdst.getReg(), getOp(inst.op1), inst.op2);
                            if (dest.isMEM()) mov(dest, mdst);
                        }
                    } else {
                        bool op2eax = false;
                        if (!dest.isMEM() && SAMEREG(i, inst.op2)) {
                            mov(eax, getOp(inst.op2));
                            op2eax = true;
                        }
                        auto& mdst = dest.isMEM() ? edx : dest;
                        if (inst.imm1) {
                            mov(mdst, inst.op1);
                        } else {
                            mov(mdst, getOp(inst.op1));
                        }
                        if (op2eax) {
                            imul(mdst.getReg(), eax);
                        } else {
                            imul(mdst.getReg(), getOp(inst.op2));
                        }
                        if (dest.isMEM()) mov(dest, mdst);
                    }
                }
                break;
            }
            case IR_SMULH:
            case IR_UMULH: {
                if (inst.imm1) {
                    mov(eax, inst.op1);
                } else {
                    mov(eax, getOp(inst.op1));
                }
                auto& msrc = inst.imm2 ? edx : getOp(inst.op2);
                if (inst.imm2) mov(edx, inst.op2);
                if (inst.opcode == IR_SMULH) {
                    imul(msrc);
                } else {
                    mul(msrc);
                }
                mov(getOp(i), edx);
                break;
            }
            case IR_SMULW: {
                if (inst.imm1) {
                    mov(eax, inst.op1);
                    movsxd(rax, eax);
                } else {
                    movsxd(rax, getOp(inst.op1));
                }
                if (inst.imm2) {
                    mov(edx, inst.op2);
                    movsxd(rdx, edx);
                } else {
                    movsxd(rdx, getOp(inst.op2));
                }
                imul(rax, rdx);
                sar(rax, 16);
                mov(getOp(i), eax);
                break;
            }
            case IR_CLZ: {
                auto& dest = getOp(i);
                if (inst.imm2) {
                    mov(edx, inst.op2);
                    lzcnt_portable(edx, edx);
                    mov(dest, edx);
                } else {
                    if (dest.isMEM()) {
                        lzcnt_portable(edx, getOp(inst.op2));
                        mov(dest, edx);
                    } else {
                        lzcnt_portable(dest.getReg(), getOp(inst.op2));
                    }
                }
                break;
            }
            case IR_REV: {
                if (inst.imm2) {
                    mov(edx, inst.op2);
                } else {
                    mov(edx, getOp(inst.op2));
                }
                bswap(edx);
                mov(getOp(i), edx);
                break;
            }
            case IR_REV16: {
                if (inst.imm2) {
                    mov(edx, inst.op2);
                } else {
                    mov(edx, getOp(inst.op2));
                }
                mov(ecx, edx);
                and_(edx, 0xff00ff00);
                and_(ecx, 0x00ff00ff);
                shr(edx, 8);
                shl(ecx, 8);
                or_(edx, ecx);
                mov(getOp(i), edx);
                break;
            }
            case IR_USAT: {
                if (inst.imm2) {
                    mov(edx, inst.op2);
                } else {
                    mov(edx, getOp(inst.op2));
                }
                mov(ecx, 0);
                cmp(edx, ecx);
                cmovl(edx, ecx);
                mov(ecx, MASK(inst.op1));
                cmp(edx, ecx);
                cmovg(edx, ecx);
                mov(getOp(i), edx);
                break;
            }
            case IR_SSAT: {
                if (inst.imm2) {
                    mov(edx, inst.op2);
                } else {
                    mov(edx, getOp(inst.op2));
                }
                mov(ecx, ~MASK(inst.op1));
                cmp(edx, ecx);
                cmovl(edx, ecx);
                mov(ecx, MASK(inst.op1));
                cmp(edx, ecx);
                cmovg(edx, ecx);
                mov(getOp(i), edx);
                break;
            }
            case IR_MEDIA_UADD8: {
                if (inst.imm2) {
                    mov(arg3, inst.op2);
                } else {
                    mov(arg3, getOp(inst.op2));
                }
                if (inst.imm1) {
                    mov(arg2, inst.op1);
                } else {
                    mov(arg2, getOp(inst.op1));
                }
                mov(arg1, rbx);
                mov(rax, (u64) media_uadd8);
                call(rax);
                mov(getOp(i), eax);
                break;
            }
            case IR_MEDIA_USUB8: {
                if (inst.imm2) {
                    mov(arg3, inst.op2);
                } else {
                    mov(arg3, getOp(inst.op2));
                }
                if (inst.imm1) {
                    mov(arg2, inst.op1);
                } else {
                    mov(arg2, getOp(inst.op1));
                }
                mov(arg1, rbx);
                mov(rax, (u64) media_usub8);
                call(rax);
                mov(getOp(i), eax);
                break;
            }
            case IR_MEDIA_UQADD8: {
                if (inst.imm1) {
                    mov(edx, inst.op1);
                } else {
                    mov(edx, getOp(inst.op1));
                }
                movd(xmm0, edx);
                if (inst.imm2) {
                    mov(edx, inst.op2);
                } else {
                    mov(edx, getOp(inst.op2));
                }
                movd(xmm1, edx);
                paddusb(xmm0, xmm1);
                movd(eax, xmm0);
                mov(getOp(i), eax);
                break;
            }
            case IR_MEDIA_UQSUB8: {
                if (inst.imm1) {
                    mov(edx, inst.op1);
                } else {
                    mov(edx, getOp(inst.op1));
                }
                movd(xmm0, edx);
                if (inst.imm2) {
                    mov(edx, inst.op2);
                } else {
                    mov(edx, getOp(inst.op2));
                }
                movd(xmm1, edx);
                psubusb(xmm0, xmm1);
                movd(eax, xmm0);
                mov(getOp(i), eax);
                break;
            }
            case IR_MEDIA_UHADD8: {
                if (inst.imm2) {
                    mov(arg3, inst.op2);
                } else {
                    mov(arg3, getOp(inst.op2));
                }
                if (inst.imm1) {
                    mov(arg2, inst.op1);
                } else {
                    mov(arg2, getOp(inst.op1));
                }
                mov(arg1, rbx);
                mov(rax, (u64) media_uhadd8);
                call(rax);
                mov(getOp(i), eax);
                break;
            }
            case IR_MEDIA_SSUB8: {
                if (inst.imm2) {
                    mov(arg3, inst.op2);
                } else {
                    mov(arg3, getOp(inst.op2));
                }
                if (inst.imm1) {
                    mov(arg2, inst.op1);
                } else {
                    mov(arg2, getOp(inst.op1));
                }
                mov(arg1, rbx);
                mov(rax, (u64) media_ssub8);
                call(rax);
                mov(getOp(i), eax);
                break;
            }
            case IR_MEDIA_QSUB8: {
                if (inst.imm1) {
                    mov(edx, inst.op1);
                } else {
                    mov(edx, getOp(inst.op1));
                }
                movd(xmm0, edx);
                if (inst.imm2) {
                    mov(edx, inst.op2);
                } else {
                    mov(edx, getOp(inst.op2));
                }
                movd(xmm1, edx);
                psubsb(xmm0, xmm1);
                movd(eax, xmm0);
                mov(getOp(i), eax);
                break;
            }
            case IR_MEDIA_SEL: {
                if (inst.imm2) {
                    mov(arg3, inst.op2);
                } else {
                    mov(arg3, getOp(inst.op2));
                }
                if (inst.imm1) {
                    mov(arg2, inst.op1);
                } else {
                    mov(arg2, getOp(inst.op1));
                }
                mov(arg1, rbx);
                mov(rax, (u64) media_sel);
                call(rax);
                mov(getOp(i), eax);
                break;
            }
            case IR_GETN: {
                auto& dest = getOp(i);
                if (inst.imm2) {
                    mov(dest, inst.op2 >> 31);
                } else {
                    if (lastflags != inst.op2) {
                        auto& src = getOp(inst.op2);
                        if (src.isMEM()) {
                            cmp(src, 0);
                        } else {
                            test(src.getReg(), src.getReg());
                        }
                        lahf();
                        lastflags = inst.op2;
                    }
                    if (dest.isMEM()) {
                        xor_(edx, edx);
                        test(ah, BIT(7));
                        setnz(dl);
                        mov(dest, edx);
                    } else {
                        xor_(dest, dest);
                        test(ah, BIT(7));
                        setnz(dest.getReg().cvt8());
                    }
                }
                break;
            }
            case IR_GETZ: {
                auto& dest = getOp(i);
                if (inst.imm2) {
                    mov(dest, inst.op2 == 0);
                } else {
                    if (lastflags != inst.op2) {
                        auto& src = getOp(inst.op2);
                        if (src.isMEM()) {
                            cmp(src, 0);
                        } else {
                            test(src.getReg(), src.getReg());
                        }
                        lahf();
                        lastflags = inst.op2;
                    }
                    if (dest.isMEM()) {
                        xor_(edx, edx);
                        test(ah, BIT(6));
                        setnz(dl);
                        mov(dest, edx);
                    } else {
                        xor_(dest, dest);
                        test(ah, BIT(6));
                        setnz(dest.getReg().cvt8());
                    }
                }
                break;
            }
            case IR_GETC: {
                if (lastflags != inst.op2) {
                    lahf();
                    seto(al);
                    lastflags = inst.op2;
                }
                auto& dest = getOp(i);
                if (dest.isMEM()) {
                    xor_(edx, edx);
                    test(ah, BIT(0));
                    setnz(dl);
                    mov(dest, edx);
                } else {
                    xor_(dest, dest);
                    test(ah, BIT(0));
                    setnz(dest.getReg().cvt8());
                }
                break;
            }
            case IR_SETC: {
                if (inst.imm2) {
                    if (inst.op2) stc();
                    else clc();
                } else {
                    mov(edx, getOp(inst.op2));
                    shr(edx, 1);
                }
                break;
            }
            case IR_GETCIFZ: {
                if (inst.imm1) {
                    if (inst.op1) {
                        if (inst.imm2) {
                            mov(getOp(i), inst.op2);
                        } else {
                            OP(mov, getOp(i), getOp(inst.op2));
                        }
                    } else {
                        auto& dest = getOp(i);
                        setc(dl);
                        movzx(edx, dl);
                        mov(dest, edx);
                    }
                } else {
                    setc(dl);
                    movzx(edx, dl);
                    auto& op1 = getOp(inst.op1);
                    if (op1.isMEM()) {
                        cmp(op1, 0);
                    } else {
                        test(op1.getReg(), op1.getReg());
                    }
                    if (inst.imm2) {
                        mov(ecx, inst.op2);
                        cmovne(edx, ecx);
                    } else {
                        cmovne(edx, getOp(inst.op2));
                    }
                    mov(getOp(i), edx);
                }
                break;
            }
            case IR_GETV: {
                if (lastflags != inst.op2) {
                    lahf();
                    seto(al);
                    lastflags = inst.op2;
                }
                auto& dest = getOp(i);
                if (dest.isMEM()) {
                    movzx(edx, al);
                    mov(dest, edx);
                } else {
                    movzx(dest.getReg(), al);
                }
                break;
            }
            case IR_PCMASK: {
                if (inst.imm1) {
                    mov(getOp(i), inst.op1 ? ~1 : ~3);
                } else {
                    auto& op1 = getOp(inst.op1);
                    auto& dest = getOp(i);
                    OP(mov, dest, op1);
                    shl(dest, 1);
                    sub(dest, 4);
                }
                break;
            }
            case IR_JZ: {
                jmptarget = inst.op2;
                if (inst.imm1) {
                    if (!inst.op1) jmp(std::to_string(nlabels), T_NEAR);
                } else {
                    auto& src = getOp(inst.op1);
                    if (src.isMEM()) {
                        cmp(src, 0);
                    } else {
                        test(src.getReg(), src.getReg());
                    }
                    jz(std::to_string(nlabels), T_NEAR);
                }
                break;
            }
            case IR_JNZ: {
                jmptarget = inst.op2;
                if (inst.imm1) {
                    if (inst.op1) jmp(std::to_string(nlabels), T_NEAR);
                } else {
                    auto& src = getOp(inst.op1);
                    if (src.isMEM()) {
                        cmp(src, 0);
                    } else {
                        test(src.getReg(), src.getReg());
                    }
                    jnz(std::to_string(nlabels), T_NEAR);
                }
                break;
            }
            case IR_JELSE: {
                jmptarget = inst.op2;
                jmp(std::to_string(nlabels + 1), T_NEAR);
                L(std::to_string(nlabels++));
                break;
            }
            case IR_MODESWITCH: {
                mov(arg1, rbx);
                mov(arg2, inst.op1);
                mov(rax, (u64) cpu_update_mode);
                call(rax);
                break;
            }
            case IR_EXCEPTION: {
                mov(arg1, rbx);
                switch (inst.op1) {
                    case E_SWI:
                        mov(arg2, (ArmInstr) {inst.op2}.sw_intr.arg);
                        mov(rax, (u64) cpu->handle_svc);
                        call(rax);
                        break;
                    case E_UND:
                        mov(arg2, inst.op2);
                        mov(rax, (u64) cpu_undefined_fail);
                        call(rax);
                        break;
                }
                break;
            }
            case IR_HALT: {
                mov(byte[CPU(halt)], 1);
                break;
            }
            case IR_BEGIN: {
                push(rbx);
                for (u32 i = 0; i < hralloc.count[REG_SAVED]; i++) {
                    push(savedregs[i].cvt64());
                }
                int spdisp = getSPDisp();
                if (spdisp) sub(rsp, spdisp);
                mov(rbx, (u64) cpu);
                L("loopblock");

                break;
            }
            case IR_END_RET:
            case IR_END_LINK:
            case IR_END_LOOP: {

                mov(rax, qword[CPU(cycles)]);
                sub(rax, inst.cycles);
                mov(qword[CPU(cycles)], rax);

                if (inst.opcode == IR_END_LOOP) {
                    cmp(rax, 0);
                    jg("loopblock");
                }

                int spdisp = getSPDisp();
                if (spdisp) add(rsp, spdisp);
                for (int i = hralloc.count[REG_SAVED] - 1; i >= 0; i--) {
                    pop(savedregs[i].cvt64());
                }
                pop(rbx);

                if (inst.opcode == IR_END_LINK) {
                    inLocalLabel();
                    cmp(rax, 0);
                    jle(".nolink");
                    links.push_back((LinkPatch) {(u32) (getCurr() - getCode()),
                                                 inst.op1, inst.op2});
                    nop(10);
                    jmp(rax);
                    L(".nolink");
                    outLocalLabel();
                }
                ret();
                break;
            }
            default:
                break;
        }
    }
}

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
                    movsd(xmm0, ptr[CPU(d[vd])]);
                    movsd(xmm1, ptr[CPU(d[vn])]);
                    mulsd(xmm1, ptr[CPU(d[vm])]);
                    subsd(xmm0, xmm1);
                    movsd(ptr[CPU(d[vd])], xmm0);
                } else {
                    movss(xmm0, ptr[CPU(s[vd])]);
                    movss(xmm1, ptr[CPU(s[vn])]);
                    mulss(xmm1, ptr[CPU(s[vm])]);
                    subss(xmm0, xmm1);
                    movss(ptr[CPU(s[vd])], xmm0);
                }
            } else {
                if (dp) {
                    movsd(xmm0, ptr[CPU(d[vn])]);
                    mulsd(xmm0, ptr[CPU(d[vm])]);
                    addsd(xmm0, ptr[CPU(d[vd])]);
                    movsd(ptr[CPU(d[vd])], xmm0);
                } else {
                    movss(xmm0, ptr[CPU(s[vn])]);
                    mulss(xmm0, ptr[CPU(s[vm])]);
                    addss(xmm0, ptr[CPU(s[vd])]);
                    movss(ptr[CPU(s[vd])], xmm0);
                }
            }
            break;
        case 1:
            if (op) {
                if (dp) {
                    movsd(xmm0, ptr[CPU(d[vn])]);
                    mulsd(xmm0, ptr[CPU(d[vm])]);
                    addsd(xmm0, ptr[CPU(d[vd])]);
                    xorpd(xmm1, xmm1);
                    subsd(xmm1, xmm0);
                    movsd(ptr[CPU(d[vd])], xmm1);
                } else {
                    movss(xmm0, ptr[CPU(s[vn])]);
                    mulss(xmm0, ptr[CPU(s[vm])]);
                    addss(xmm0, ptr[CPU(s[vd])]);
                    xorps(xmm1, xmm1);
                    subss(xmm1, xmm0);
                    movss(ptr[CPU(s[vd])], xmm1);
                }
            } else {
                if (dp) {
                    movsd(xmm0, ptr[CPU(d[vn])]);
                    mulsd(xmm0, ptr[CPU(d[vm])]);
                    subsd(xmm0, ptr[CPU(d[vd])]);
                    movsd(ptr[CPU(d[vd])], xmm0);
                } else {
                    movss(xmm0, ptr[CPU(s[vn])]);
                    mulss(xmm0, ptr[CPU(s[vm])]);
                    subss(xmm0, ptr[CPU(s[vd])]);
                    movss(ptr[CPU(s[vd])], xmm0);
                }
            }
            break;
        case 2:
            if (dp) {
                movsd(xmm0, ptr[CPU(d[vn])]);
                mulsd(xmm0, ptr[CPU(d[vm])]);
                if (op) {
                    xorpd(xmm1, xmm1);
                    subsd(xmm1, xmm0);
                    movsd(ptr[CPU(d[vd])], xmm1);
                } else {
                    movsd(ptr[CPU(d[vd])], xmm0);
                }
            } else {
                movss(xmm0, ptr[CPU(s[vn])]);
                mulss(xmm0, ptr[CPU(s[vm])]);
                if (op) {
                    xorps(xmm1, xmm1);
                    subss(xmm1, xmm0);
                    movss(ptr[CPU(s[vd])], xmm1);
                } else {
                    movss(ptr[CPU(s[vd])], xmm0);
                }
            }
            break;
        case 3:
            if (op) {
                if (dp) {
                    movsd(xmm0, ptr[CPU(d[vn])]);
                    subsd(xmm0, ptr[CPU(d[vm])]);
                    movsd(ptr[CPU(d[vd])], xmm0);
                } else {
                    movss(xmm0, ptr[CPU(s[vn])]);
                    subss(xmm0, ptr[CPU(s[vm])]);
                    movss(ptr[CPU(s[vd])], xmm0);
                }
            } else {
                if (dp) {
                    movsd(xmm0, ptr[CPU(d[vn])]);
                    addsd(xmm0, ptr[CPU(d[vm])]);
                    movsd(ptr[CPU(d[vd])], xmm0);
                } else {
                    movss(xmm0, ptr[CPU(s[vn])]);
                    addss(xmm0, ptr[CPU(s[vm])]);
                    movss(ptr[CPU(s[vd])], xmm0);
                }
            }
            break;
        case 8:
            if (dp) {
                movsd(xmm0, ptr[CPU(d[vn])]);
                divsd(xmm0, ptr[CPU(d[vm])]);
                movsd(ptr[CPU(d[vd])], xmm0);
            } else {
                movss(xmm0, ptr[CPU(s[vn])]);
                divss(xmm0, ptr[CPU(s[vm])]);
                movss(ptr[CPU(s[vd])], xmm0);
            }
            break;
        case 11: {
            op = instr.cp_data_proc.cp & 4;
            switch (instr.cp_data_proc.crn) {
                case 0:
                    if (op) {
                        if (dp) {
                            mov(rdx, MASKL(63));
                            and_(rdx, ptr[CPU(d[vm])]);
                            mov(ptr[CPU(d[vd])], rdx);
                        } else {
                            mov(edx, ptr[CPU(s[vm])]);
                            and_(edx, MASK(31));
                            mov(ptr[CPU(s[vd])], edx);
                        }
                    } else {
                        if (dp) {
                            movsd(xmm0, ptr[CPU(d[vm])]);
                            movsd(ptr[CPU(d[vd])], xmm0);
                        } else {
                            movss(xmm0, ptr[CPU(s[vm])]);
                            movss(ptr[CPU(s[vd])], xmm0);
                        }
                    }
                    break;
                case 1:
                    if (op) {
                        if (dp) {
                            sqrtsd(xmm0, ptr[CPU(d[vm])]);
                            movsd(ptr[CPU(d[vd])], xmm0);
                        } else {
                            sqrtss(xmm0, ptr[CPU(s[vm])]);
                            movss(ptr[CPU(s[vd])], xmm0);
                        }
                    } else {
                        if (dp) {
                            mov(rdx, BITL(63));
                            xor_(rdx, ptr[CPU(d[vm])]);
                            mov(ptr[CPU(d[vd])], rdx);
                        } else {
                            mov(edx, ptr[CPU(s[vm])]);
                            xor_(edx, BIT(31));
                            mov(ptr[CPU(s[vd])], edx);
                        }
                    }
                    break;
                case 4:
                case 5:
                    if (dp) {
                        movsd(xmm0, ptr[CPU(d[vd])]);
                        if (instr.cp_data_proc.crn & 1) {
                            xorpd(xmm1, xmm1);
                            comisd(xmm0, xmm1);
                        } else {
                            comisd(xmm0, ptr[CPU(d[vm])]);
                        }
                    } else {
                        movss(xmm0, ptr[CPU(s[vd])]);
                        if (instr.cp_data_proc.crn & 1) {
                            xorps(xmm1, xmm1);
                            comiss(xmm0, xmm1);
                        } else {
                            comiss(xmm0, ptr[CPU(s[vm])]);
                        }
                    }
                    // x86 flags to arm flags
                    // 00 -> 0010 (gt)
                    // 01 -> 1000 (lt)
                    // 10 -> 0110 (eq)
                    // 11 -> 0011 (uo)
                    inLocalLabel();
                    lahf();
                    and_(dword[CPU(fpscr)], 0x0fff'ffff);
                    and_(ah, 0x41);
                    jz(".l0");
                    cmp(ah, 0x01);
                    jz(".l1");
                    cmp(ah, 0x40);
                    jz(".l2");
                    or_(dword[CPU(fpscr)], 3 << 28);
                    jmp(".end");
                    L(".l2");
                    or_(dword[CPU(fpscr)], 6 << 28);
                    jmp(".end");
                    L(".l1");
                    or_(dword[CPU(fpscr)], 8 << 28);
                    jmp(".end");
                    L(".l0");
                    or_(dword[CPU(fpscr)], 2 << 28);
                    L(".end");
                    outLocalLabel();
                    break;
                case 7:
                    if (dp) {
                        vd = vd << 1 | ((instr.cp_data_proc.cpopc >> 2) & 1);

                        cvtsd2ss(xmm0, ptr[CPU(d[vm])]);
                        movss(ptr[CPU(s[vd])], xmm0);
                    } else {
                        vd = vd >> 1;

                        cvtss2sd(xmm0, ptr[CPU(s[vm])]);
                        movsd(ptr[CPU(d[vd])], xmm0);
                    }
                    break;
                case 8:
                    if (dp) {
                        vm = vm << 1 | (instr.cp_data_proc.cp & 1);

                        if (op) {
                            cvtsi2sd(xmm0, ptr[CPU(s[vm])]);
                            movsd(ptr[CPU(d[vd])], xmm0);
                        } else {
                            mov(edx, ptr[CPU(s[vm])]);
                            cvtsi2sd(xmm0, rdx);
                            movsd(ptr[CPU(d[vd])], xmm0);
                        }
                    } else {
                        if (op) {
                            cvtsi2ss(xmm0, ptr[CPU(s[vm])]);
                            movss(ptr[CPU(s[vd])], xmm0);
                        } else {
                            mov(edx, ptr[CPU(s[vm])]);
                            cvtsi2ss(xmm0, rdx);
                            movss(ptr[CPU(s[vd])], xmm0);
                        }
                    }
                    break;
                case 12:
                case 13:
                    inLocalLabel();
                    if (dp) {
                        vd = vd << 1 | ((instr.cp_data_proc.cpopc >> 2) & 1);

                        if (instr.cp_data_proc.crn & 1) {
                            cvttsd2si(edx, ptr[CPU(d[vm])]);
                            cmp(edx, BIT(31));
                            jnz(".nooverflow");
                            mov(edx, MASK(31));
                            L(".nooverflow");
                            mov(ptr[CPU(s[vd])], edx);
                        } else {
                            cvttsd2si(rdx, ptr[CPU(d[vm])]);
                            mov(rax, BITL(63));
                            cmp(rdx, rax);
                            jnz(".nooverflow");
                            mov(edx, MASKL(32));
                            L(".nooverflow");
                            mov(ptr[CPU(s[vd])], edx);
                        }
                    } else {
                        if (instr.cp_data_proc.crn & 1) {
                            cvttss2si(edx, ptr[CPU(s[vm])]);
                            cmp(edx, BIT(31));
                            jnz(".nooverflow");
                            mov(edx, MASK(31));
                            L(".nooverflow");
                            mov(ptr[CPU(s[vd])], edx);
                        } else {
                            cvttss2si(rdx, ptr[CPU(s[vm])]);
                            mov(rax, BITL(63));
                            cmp(rdx, rax);
                            jnz(".nooverflow");
                            mov(edx, MASKL(32));
                            L(".nooverflow");
                            mov(ptr[CPU(s[vd])], edx);
                        }
                    }
                    outLocalLabel();
                    break;
            }
            break;
        }
    }
}

void Code::compileVFPRead(ArmInstr instr, const Xbyak::Operand& dst) {
    if (instr.cp_reg_trans.cpopc == 7) {
        if (instr.cp_reg_trans.crn == 1) {
            OP(mov, dst, ptr[CPU(fpscr)]);
        } else {
            lwarn("unknown vfp special reg %d", instr.cp_reg_trans.crn);
            mov(dst, 0);
        }
        return;
    }

    u32 vn = instr.cp_reg_trans.crn << 1;
    if (instr.cp_reg_trans.cpnum & 1) vn |= instr.cp_reg_trans.cpopc & 1;
    else vn |= instr.cp_reg_trans.cp >> 2;

    OP(mov, dst, ptr[CPU(s[vn])]);
}

void Code::compileVFPWrite(ArmInstr instr, const Xbyak::Operand& src) {
    if (instr.cp_reg_trans.cpopc == 7) {
        if (instr.cp_reg_trans.crn == 1) {
            OP(mov, ptr[CPU(fpscr)], src);
        } else {
            lwarn("unknown vfp special reg %d", instr.cp_reg_trans.crn);
        }
        return;
    }

    u32 vn = instr.cp_reg_trans.crn << 1;
    if (instr.cp_reg_trans.cpnum & 1) vn |= instr.cp_reg_trans.cpopc & 1;
    else vn |= instr.cp_reg_trans.cp >> 2;

    OP(mov, ptr[CPU(s[vn])], src);
}

void Code::compileVFPRead64(ArmInstr instr, const Xbyak::Operand& dst,
                            bool hi) {
    if (instr.cp_double_reg_trans.cpnum & 1) {
        u32 vm = instr.cp_double_reg_trans.crm;
        if (hi) {
            OP(mov, dst, ptr[CPU(d[vm]) + 4]);
        } else {
            OP(mov, dst, ptr[CPU(d[vm])]);
        }
    } else {
        u32 vm = instr.cp_double_reg_trans.crm << 1 |
                 ((instr.cp_double_reg_trans.cp >> 1) & 1);
        if (hi) {
            OP(mov, dst, ptr[CPU(s[vm + 1])]);
        } else {
            OP(mov, dst, ptr[CPU(s[vm])]);
        }
    }
}

void Code::compileVFPWrite64(ArmInstr instr, const Xbyak::Operand& src,
                             bool hi) {
    if (instr.cp_double_reg_trans.cpnum & 1) {
        u32 vm = instr.cp_double_reg_trans.crm;
        if (hi) {
            OP(mov, ptr[CPU(d[vm]) + 4], src);
        } else {
            OP(mov, ptr[CPU(d[vm])], src);
        }
    } else {
        u32 vm = instr.cp_double_reg_trans.crm << 1 |
                 ((instr.cp_double_reg_trans.cp >> 1) & 1);
        if (hi) {
            if (vm < 31) OP(mov, ptr[CPU(s[vm + 1])], src);
        } else {
            OP(mov, ptr[CPU(s[vm])], src);
        }
    }
}

void Code::compileVFPLoadMem(ArmInstr instr, const Xbyak::Operand& _addr) {
    u32 rcount;
    if (instr.cp_data_trans.p && !instr.cp_data_trans.w) {
        rcount = 1;
    } else {
        rcount = instr.cp_data_trans.offset;
        if (instr.cp_data_trans.cpnum & 1) rcount >>= 1;
    }

    u32 vd = instr.cp_data_trans.crd;

    const Xbyak::Operand* addr_ = &_addr;
    if (rcount > 1) {
        // need to set ebp with addr before modifying rsp
        mov(ptr[rsp - 8], rbp);
        mov(ebp, _addr);
        sub(rsp, 16);
        addr_ = &ebp;
    }
    auto& addr = *addr_;

    if (instr.cp_data_trans.cpnum & 1) {
        for (int i = 0; i < rcount; i++) {
#ifdef JIT_FASTMEM
            mov(rax, (u64) cpu->fastmem);
            if (addr != edx) mov(edx, addr);
            movsd(xmm0, ptr[rax + rdx]);
#else
            mov(arg2, addr);
            mov(arg1, rbx);
            mov(rax, (u64) cpu->readf64);
            call(rax);
#endif
            movsd(ptr[CPU(d[(vd + i) & 15])], xmm0);
            if (i < rcount - 1) add(addr, 8);
        }
    } else {
        vd = vd << 1 | instr.cp_data_trans.n;

        for (int i = 0; i < rcount; i++) {
#ifdef JIT_FASTMEM
            mov(rax, (u64) cpu->fastmem);
            if (addr != edx) mov(edx, addr);
            movss(xmm0, ptr[rax + rdx]);
#else
            mov(arg2, addr);
            mov(arg1, rbx);
            mov(rax, (u64) cpu->readf32);
            call(rax);
#endif
            movss(ptr[CPU(s[(vd + i) & 31])], xmm0);
            if (i < rcount - 1) add(addr, 4);
        }
    }

    if (rcount > 1) {
        add(rsp, 8);
        pop(rbp);
    }
}

void Code::compileVFPStoreMem(ArmInstr instr, const Xbyak::Operand& _addr) {
    u32 rcount;
    if (instr.cp_data_trans.p && !instr.cp_data_trans.w) {
        rcount = 1;
    } else {
        rcount = instr.cp_data_trans.offset;
        if (instr.cp_data_trans.cpnum & 1) rcount >>= 1;
    }

    u32 vd = instr.cp_data_trans.crd;

    const Xbyak::Operand* addr_ = &_addr;
    if (rcount > 1) {
        // need to set ebp with addr before modifying rsp
        mov(ptr[rsp - 8], rbp);
        mov(ebp, _addr);
        sub(rsp, 16);
        addr_ = &ebp;
    }
    auto& addr = *addr_;

    if (instr.cp_data_trans.cpnum & 1) {
        for (int i = 0; i < rcount; i++) {
            movsd(arg3f, ptr[CPU(d[(vd + i) & 15])]);
#ifdef JIT_FASTMEM
            mov(rax, (u64) cpu->fastmem);
            if (addr != edx) mov(edx, addr);
            movsd(ptr[rax + rdx], arg3f);
#else
            mov(arg2, addr);
            mov(arg1, rbx);
            mov(rax, (u64) cpu->writef64);
            call(rax);
#endif
            if (i < rcount - 1) add(addr, 8);
        }
    } else {
        vd = vd << 1 | instr.cp_data_trans.n;

        for (int i = 0; i < rcount; i++) {
            movss(arg3f, ptr[CPU(s[(vd + i) & 31])]);
#ifdef JIT_FASTMEM
            mov(rax, (u64) cpu->fastmem);
            if (addr != edx) mov(edx, addr);
            movss(ptr[rax + rdx], arg3f);
#else
            mov(arg2, addr);
            mov(arg1, rbx);
            mov(rax, (u64) cpu->writef32);
            call(rax);
#endif
            if (i < rcount - 1) add(addr, 4);
        }
    }

    if (rcount > 1) {
        add(rsp, 8);
        pop(rbp);
    }
}

extern "C" {

void* backend_x86_generate_code(IRBlock* ir, RegAllocation* regalloc,
                                ArmCore* cpu) {
    return new Code(ir, regalloc, cpu);
}

JITFunc backend_x86_get_code(void* backend) {
    return (JITFunc) ((Code*) backend)->getCode();
}

void backend_x86_patch_links(JITBlock* block) {
    Code* code = (Code*) block->backend;
    for (auto [offset, attrs, addr] : code->links) {
        char* jmpsrc = (char*) code->getCode() + offset;
        JITBlock* linkblock = get_jitblock(code->cpu, attrs, addr);
        jmpsrc[0] = 0x48;
        jmpsrc[1] = 0xb8;
        *(u64*) &jmpsrc[2] = (u64) linkblock->code;
        Vec_push(linkblock->linkingblocks,
                 ((BlockLocation) {block->attrs, block->start_addr}));
    }

    code->readyRE();
}

void backend_x86_free(void* backend) {
    delete ((Code*) backend);
}

#ifndef NOCAPSTONE
void backend_x86_disassemble(void* backend) {
    Code* code = (Code*) backend;
    code->print_hostregs();
    csh handle;
    cs_insn* insn;
    cs_open(CS_ARCH_X86, CS_MODE_64, &handle);
    cs_option(handle, CS_OPT_SKIPDATA, CS_OPT_ON);
    size_t count =
        cs_disasm(handle, code->getCode(), code->getSize(), 0, 0, &insn);
    printf("--------- JIT Disassembly at %p ------------\n", code->getCode());
    for (size_t i = 0; i < count; i++) {
        printf("%04lx: %s %s\n", insn[i].address, insn[i].mnemonic,
               insn[i].op_str);
    }
    cs_free(insn, count);
}
#endif
}

#endif