#ifdef __x86_64__

#include "backend_x86.h"

#ifndef NOCAPSTONE
#include <capstone/capstone.h>
#endif

#include "arm/media.h"

#define RAS_CTX_VAR this->code
#include <ras/ras_x64.h>

#ifdef _WIN32
#define ARG1 RCX
#define ARG2 RDX
#define ARG3 R8
#define ARG3F XMM2
#else
#define ARG1 RDI
#define ARG2 RSI
#define ARG3 RDX
#define ARG3F XMM0
#endif

static const rasX64Reg tempregs[] =
#ifdef _WIN32
    {R9, R10, R11};
#else
    {RSI, RDI, R8, R9, R10, R11};
#endif
static const rasX64Reg savedregs[] =
#ifdef _WIN32
    {RBP, RDI, RSI, R12, R13, R14, R15};
#else
    {RBP, R12, R13, R14, R15};
#endif

static rasX64Op getOpForReg(X86CodeBackend* this, int i) {
    HostRegInfo hr = this->hralloc.hostreg_info[i];
    switch (hr.type) {
        case REG_TEMP:
            return MKOP(tempregs[hr.index]);
        case REG_SAVED:
            return MKOP(savedregs[hr.index]);
        case REG_STACK:
            return MKOP(PTR(4 * hr.index, RSP));
        default:
            unreachable();
    }
}

[[maybe_unused]]
static void print_hostregs(X86CodeBackend* this) {
    printf("Host Regs:");
    for (u32 i = 0; i < this->hralloc.nregs; i++) {
        printf(" $%d:", i);
        auto operand = getOpForReg(this, i);
        if (operand.isMem)
            printf("[rsp+%d]", 4 * this->hralloc.hostreg_info[i].index);
        else printf("r%d", operand.r.idx);
    }
    printf("\n");
}

static int getSPDisp(X86CodeBackend* this) {
    int disp = (this->hralloc.count[REG_SAVED] + 2) * 8 +
               this->hralloc.count[REG_STACK] * 4;
    disp = (disp + 15) & ~15;
    disp -= (this->hralloc.count[REG_SAVED] + 2) * 8;
    return disp;
}

static rasX64Op getOp(X86CodeBackend* this, int i) {
    return getOpForReg(this, this->regalloc->reg_assn[i]);
}

static void compileVFPDataProc(X86CodeBackend* this, ArmInstr instr);
static void compileVFPLoadMem(X86CodeBackend* this, ArmInstr instr,
                              rasX64Op addr);
static void compileVFPStoreMem(X86CodeBackend* this, ArmInstr instr,
                               rasX64Op addr);
static void compileVFPRead(X86CodeBackend* this, ArmInstr instr, rasX64Op dst);
static void compileVFPWrite(X86CodeBackend* this, ArmInstr instr, rasX64Op src);
static void compileVFPRead64(X86CodeBackend* this, ArmInstr instr, rasX64Op dst,
                             bool hi);
static void compileVFPWrite64(X86CodeBackend* this, ArmInstr instr,
                              rasX64Op src, bool hi);

#define GETOP(i) getOp(this, i)

#define CPU(m, ...) PTR(offsetof(ArmCore, m) __VA_OPT__(+) __VA_ARGS__, RBX)

#define OP(op, dest, src)                                                      \
    ({                                                                         \
        auto d = MKOP(dest);                                                   \
        auto s = MKOP(src);                                                    \
        if (s.isMem && d.isMem) {                                              \
            MOVD(RDX, s);                                                      \
            op(d, RDX);                                                        \
        } else if (s.isMem) op(d.r, src);                                      \
        else op(d, s.r);                                                       \
    })

#define LOAD(addr)                                                             \
    ({                                                                         \
        auto dest = GETOP(i);                                                  \
        OP(MOVD, dest, addr);                                                  \
    })

#define STORE(addr)                                                            \
    ({                                                                         \
        if (inst.imm2) {                                                       \
            MOVD(addr, inst.op2);                                              \
        } else {                                                               \
            auto src = GETOP(inst.op2);                                        \
            OP(MOVD, addr, src);                                               \
        }                                                                      \
    })

#define SAMEREG(v1, v2) (regalloc->reg_assn[v1] == regalloc->reg_assn[v2])

#define BINARY(op)                                                             \
    ({                                                                         \
        bool op2eax = false;                                                   \
        if (!inst.imm2 && SAMEREG(inst.op2, i)) {                              \
            MOVD(RAX, GETOP(inst.op2));                                        \
            op2eax = true;                                                     \
        }                                                                      \
        auto dest = GETOP(i);                                                  \
        if (inst.imm1) {                                                       \
            MOVD(dest, inst.op1);                                              \
        } else if (!SAMEREG(inst.op1, i)) {                                    \
            OP(MOVD, dest, GETOP(inst.op1));                                   \
        }                                                                      \
        if (op2eax) {                                                          \
            op(dest, RAX);                                                     \
        } else if (inst.imm2) {                                                \
            op(dest, inst.op2);                                                \
        } else {                                                               \
            OP(op, dest, GETOP(inst.op2));                                     \
        }                                                                      \
    })

X86CodeBackend* backend_x86_generate_code(IRBlock* ir, RegAllocation* regalloc,
                                          ArmCore* cpu) {
    X86CodeBackend* this = calloc(1, sizeof *this);
    this->code = rasCreate(16384, 0);
    this->cpu = cpu;
    this->regalloc = regalloc;

    this->hralloc = allocate_host_registers(regalloc, countof(tempregs),
                                            countof(savedregs));

    u32 flags_mask = 0;
    u32 lastflags = 0;
    u32 jmptarget = -1;

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
                auto dest = GETOP(i);
                SHRD(dest, 31 - inst.op1);
                ANDD(dest, 1);
                break;
            }
            case IR_STORE_FLAG: {
                if (ir->code.d[i - 2].opcode != IR_STORE_FLAG) {
                    XORD(RCX, RCX);
                    flags_mask = 0;
                }
                if (inst.imm2) {
                    if (inst.op2) ORD(RCX, BIT(31 - inst.op1));
                } else {
                    MOVD(RDX, GETOP(inst.op2));
                    SHLD(RDX, 31 - inst.op1);
                    ORD(RCX, RDX);
                }
                flags_mask |= BIT(31 - inst.op1);
                if (ir->code.d[i + 2].opcode != IR_STORE_FLAG) {
                    ANDD(CPU(cpsr), ~flags_mask);
                    ORD(CPU(cpsr), RCX);
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
                auto dest = GETOP(i);
                SHRD(dest, 5);
                ANDD(dest, 1);
                break;
            }
            case IR_STORE_THUMB: {
                ANDD(CPU(cpsr), ~BIT(5));
                if (inst.imm2) {
                    if (inst.op2) ORD(CPU(cpsr), BIT(5));
                } else {
                    MOVD(RDX, GETOP(inst.op2));
                    SHLD(RDX, 5);
                    ORD(CPU(cpsr), RDX);
                }
                break;
            }
            case IR_VFP_DATA_PROC: {
                compileVFPDataProc(this, (ArmInstr) {inst.op1});
                break;
            }
            case IR_VFP_LOAD_MEM: {
                if (inst.imm2) {
                    MOVD(RDX, inst.op2);
                    compileVFPLoadMem(this, (ArmInstr) {inst.op1}, MKOP(RDX));
                } else {
                    compileVFPLoadMem(this, (ArmInstr) {inst.op1},
                                      GETOP(inst.op2));
                }
                break;
            }
            case IR_VFP_STORE_MEM: {
                if (inst.imm2) {
                    MOVD(RDX, inst.op2);
                    compileVFPStoreMem(this, (ArmInstr) {inst.op1}, MKOP(RDX));
                } else {
                    compileVFPStoreMem(this, (ArmInstr) {inst.op1},
                                       GETOP(inst.op2));
                }
                break;
            }
            case IR_VFP_READ: {
                compileVFPRead(this, (ArmInstr) {inst.op1}, GETOP(i));
                break;
            }
            case IR_VFP_WRITE: {
                if (inst.imm2) {
                    MOVD(RDX, inst.op2);
                    compileVFPWrite(this, (ArmInstr) {inst.op1}, MKOP(RDX));
                } else {
                    compileVFPWrite(this, (ArmInstr) {inst.op1},
                                    GETOP(inst.op2));
                }
                break;
            }
            case IR_VFP_READ64L: {
                compileVFPRead64(this, (ArmInstr) {inst.op1}, GETOP(i), false);
                break;
            }
            case IR_VFP_READ64H:
                compileVFPRead64(this, (ArmInstr) {inst.op1}, GETOP(i), true);
                break;
            case IR_VFP_WRITE64L: {
                if (inst.imm2) {
                    MOVD(RDX, inst.op2);
                    compileVFPWrite64(this, (ArmInstr) {inst.op1}, MKOP(RDX),
                                      false);
                } else {
                    compileVFPWrite64(this, (ArmInstr) {inst.op1},
                                      GETOP(inst.op2), false);
                }
                break;
            }
            case IR_VFP_WRITE64H:
                if (inst.imm2) {
                    MOVD(RDX, inst.op2);
                    compileVFPWrite64(this, (ArmInstr) {inst.op1}, MKOP(RDX),
                                      true);
                } else {
                    compileVFPWrite64(this, (ArmInstr) {inst.op1},
                                      GETOP(inst.op2), true);
                }
                break;
                break;
            case IR_CP15_READ: {
                MOVQ(ARG1, RBX);
                MOVD(ARG2, inst.op1);
                MOVQ(RAX, (uintptr_t) cpu->cp15_read);
                CALL(RAX);
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_CP15_WRITE: {
                if (inst.imm2) {
                    MOVD(ARG3, inst.op2);
                } else {
                    MOVD(ARG3, GETOP(inst.op2));
                }
                MOVQ(ARG1, RBX);
                MOVD(ARG2, inst.op1);
                MOVQ(RAX, (uintptr_t) cpu->cp15_write);
                CALL(RAX);
                break;
            }
#ifdef JIT_FASTMEM
            case IR_LOAD_MEM8: {
                MOVQ(RAX, (uintptr_t) cpu->fastmem);
                if (inst.imm1) {
                    MOVD(RDX, inst.op1);
                } else {
                    MOVD(RDX, GETOP(inst.op1));
                }
                MOVZXBD(RAX, PTR(RAX, RDX));
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_LOAD_MEMS8: {
                MOVQ(RAX, (uintptr_t) cpu->fastmem);
                if (inst.imm1) {
                    MOVD(RDX, inst.op1);
                } else {
                    MOVD(RDX, GETOP(inst.op1));
                }
                MOVSXBD(RAX, PTR(RAX, RDX));
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_LOAD_MEM16: {
                MOVQ(RAX, (uintptr_t) cpu->fastmem);
                if (inst.imm1) {
                    MOVD(RDX, inst.op1);
                } else {
                    MOVD(RDX, GETOP(inst.op1));
                }
                MOVZXWD(RAX, PTR(RAX, RDX));
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_LOAD_MEMS16: {
                MOVQ(RAX, (uintptr_t) cpu->fastmem);
                if (inst.imm1) {
                    MOVD(RDX, inst.op1);
                } else {
                    MOVD(RDX, GETOP(inst.op1));
                }
                MOVSXWD(RAX, PTR(RAX, RDX));
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_LOAD_MEM32: {
                MOVQ(RAX, (uintptr_t) cpu->fastmem);
                if (inst.imm1) {
                    MOVD(RDX, inst.op1);
                } else {
                    MOVD(RDX, GETOP(inst.op1));
                }
                MOVD(RAX, PTR(RAX, RDX));
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_STORE_MEM8: {
                MOVQ(RAX, (uintptr_t) cpu->fastmem);
                if (inst.imm1) {
                    MOVD(RDX, inst.op1);
                } else {
                    MOVD(RDX, GETOP(inst.op1));
                }
                if (inst.imm2) {
                    MOVD(RCX, inst.op2);
                } else {
                    MOVD(RCX, GETOP(inst.op2));
                }
                MOVB(PTR(RAX, RDX), RCX);
                break;
            }
            case IR_STORE_MEM16: {
                MOVQ(RAX, (uintptr_t) cpu->fastmem);
                if (inst.imm1) {
                    MOVD(RDX, inst.op1);
                } else {
                    MOVD(RDX, GETOP(inst.op1));
                }
                if (inst.imm2) {
                    MOVD(RCX, inst.op2);
                } else {
                    MOVD(RCX, GETOP(inst.op2));
                }
                MOVW(PTR(RAX, RDX), RCX);
                break;
            }
            case IR_STORE_MEM32: {
                MOVQ(RAX, (uintptr_t) cpu->fastmem);
                if (inst.imm1) {
                    MOVD(RDX, inst.op1);
                } else {
                    MOVD(RDX, GETOP(inst.op1));
                }
                if (inst.imm2) {
                    MOVD(RCX, inst.op2);
                } else {
                    MOVD(RCX, GETOP(inst.op2));
                }
                MOVD(PTR(RAX, RDX), RCX);
                break;
            }
#else
            case IR_LOAD_MEM8: {
                XORD(ARG3, ARG3);
                if (inst.imm1) {
                    MOVD(ARG2, inst.op1);
                } else {
                    auto src = GETOP(inst.op1);
                    if (src.isMem || src.r.idx != ARG2.idx) MOVD(ARG2, src);
                }
                MOVQ(ARG1, RBX);
                MOVQ(RAX, (uintptr_t) cpu->read8);
                CALL(RAX);
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_LOAD_MEMS8: {
                MOVD(ARG3, 1);
                if (inst.imm1) {
                    MOVD(ARG2, inst.op1);
                } else {
                    auto src = GETOP(inst.op1);
                    if (src.isMem || src.r.idx != ARG2.idx) MOVD(ARG2, src);
                }
                MOVQ(ARG1, RBX);
                MOVQ(RAX, (u64) cpu->read8);
                CALL(RAX);
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_LOAD_MEM16: {
                XORD(ARG3, ARG3);
                if (inst.imm1) {
                    MOVD(ARG2, inst.op1);
                } else {
                    auto src = GETOP(inst.op1);
                    if (src.isMem || src.r.idx != ARG2.idx) MOVD(ARG2, src);
                }
                MOVQ(ARG1, RBX);
                MOVQ(RAX, (u64) cpu->read16);
                CALL(RAX);
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_LOAD_MEMS16: {
                MOVD(ARG3, 1);
                if (inst.imm1) {
                    MOVD(ARG2, inst.op1);
                } else {
                    auto src = GETOP(inst.op1);
                    if (src.isMem || src.r.idx != ARG2.idx) MOVD(ARG2, src);
                }
                MOVQ(ARG1, RBX);
                MOVQ(RAX, (u64) cpu->read16);
                CALL(RAX);
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_LOAD_MEM32: {
                if (inst.imm1) {
                    MOVD(ARG2, inst.op1);
                } else {
                    auto src = GETOP(inst.op1);
                    if (src.isMem || src.r.idx != ARG2.idx) MOVD(ARG2, src);
                }
                MOVQ(ARG1, RBX);
                MOVQ(RAX, (u64) cpu->read32);
                CALL(RAX);
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_STORE_MEM8: {
                if (inst.imm2) {
                    MOVD(ARG3, inst.op2);
                } else {
                    MOVD(ARG3, GETOP(inst.op2));
                }
                if (inst.imm1) {
                    MOVD(ARG2, inst.op1);
                } else {
                    auto src = GETOP(inst.op1);
                    if (src.isMem || src.r.idx != ARG2.idx) MOVD(ARG2, src);
                }
                MOVQ(ARG1, RBX);
                MOVQ(RAX, (u64) cpu->write8);
                CALL(RAX);
                break;
            }
            case IR_STORE_MEM16: {
                if (inst.imm2) {
                    MOVD(ARG3, inst.op2);
                } else {
                    MOVD(ARG3, GETOP(inst.op2));
                }
                if (inst.imm1) {
                    MOVD(ARG2, inst.op1);
                } else {
                    auto src = GETOP(inst.op1);
                    if (src.isMem || src.r.idx != ARG2.idx) MOVD(ARG2, src);
                }
                MOVQ(ARG1, RBX);
                MOVQ(RAX, (u64) cpu->write16);
                CALL(RAX);
                break;
            }
            case IR_STORE_MEM32: {
                if (inst.imm2) {
                    MOVD(ARG3, inst.op2);
                } else {
                    MOVD(ARG3, GETOP(inst.op2));
                }
                if (inst.imm1) {
                    MOVD(ARG2, inst.op1);
                } else {
                    auto src = GETOP(inst.op1);
                    if (src.isMem || src.r.idx != ARG2.idx) MOVD(ARG2, src);
                }
                MOVQ(ARG1, RBX);
                MOVQ(RAX, (u64) cpu->write32);
                CALL(RAX);
                break;
            }
#endif
            case IR_MOV: {
                auto dst = GETOP(i);
                if (inst.imm2) {
                    MOVD(dst, inst.op2);
                } else if (!SAMEREG(inst.op2, i)) {
                    OP(MOVD, dst, GETOP(inst.op2));
                }
                break;
            }
            case IR_AND:
                BINARY(ANDD);
                break;
            case IR_OR:
                BINARY(ORD);
                break;
            case IR_XOR:
                BINARY(XORD);
                break;
            case IR_NOT: {
                auto dst = GETOP(i);
                if (inst.imm2) {
                    MOVD(dst, inst.op2);
                } else if (!SAMEREG(inst.op2, i)) {
                    OP(MOVD, dst, GETOP(inst.op2));
                }
                NOTD(dst);
                break;
            }
            case IR_LSL: {
                bool op2eax = false;
                if (!inst.imm2 && SAMEREG(inst.op2, i)) {
                    MOVD(RAX, GETOP(inst.op2));
                    op2eax = true;
                }
                auto dest = GETOP(i);
                if (inst.imm1) {
                    MOVD(dest, inst.op1);
                } else if (!SAMEREG(inst.op1, i)) {
                    OP(MOVD, dest, GETOP(inst.op1));
                }
                if (inst.imm2) {
                    if (inst.op2 >= 32) {
                        MOVD(dest, 0);
                    } else {
                        SHLD(dest, inst.op2);
                    }
                } else {
                    if (op2eax) {
                        MOVD(RCX, RAX);
                    } else {
                        MOVD(RCX, GETOP(inst.op2));
                    }
                    LABEL(lnormal);
                    LABEL(lend);
                    CMPB(RCX, 32);
                    JB(lnormal);
                    MOVD(dest, 0);
                    JMP(lend);
                    L(lnormal);
                    SHLCD(dest);
                    L(lend);
                }
                break;
            }
            case IR_LSR: {
                bool op2eax = false;
                if (!inst.imm2 && SAMEREG(inst.op2, i)) {
                    MOVD(RAX, GETOP(inst.op2));
                    op2eax = true;
                }
                auto dest = GETOP(i);
                if (inst.imm1) {
                    MOVD(dest, inst.op1);
                } else if (!SAMEREG(inst.op1, i)) {
                    OP(MOVD, dest, GETOP(inst.op1));
                }
                if (inst.imm2) {
                    if (inst.op2 >= 32) {
                        MOVD(dest, 0);
                    } else {
                        SHRD(dest, inst.op2);
                    }
                } else {
                    if (op2eax) {
                        MOVD(RCX, RAX);
                    } else {
                        MOVD(RCX, GETOP(inst.op2));
                    }
                    LABEL(lnormal);
                    LABEL(lend);
                    CMPB(RCX, 32);
                    JB(lnormal);
                    MOVD(dest, 0);
                    JMP(lend);
                    L(lnormal);
                    SHRCD(dest);
                    L(lend);
                }
                break;
            }
            case IR_ASR: {
                bool op2eax = false;
                if (!inst.imm2 && SAMEREG(inst.op2, i)) {
                    MOVD(RAX, GETOP(inst.op2));
                    op2eax = true;
                }
                auto dest = GETOP(i);
                if (inst.imm1) {
                    MOVD(dest, inst.op1);
                } else if (!SAMEREG(inst.op1, i)) {
                    OP(MOVD, dest, GETOP(inst.op1));
                }
                if (inst.imm2) {
                    if (inst.op2 >= 32) {
                        SARD(dest, 31);
                    } else {
                        SARD(dest, inst.op2);
                    }
                } else {
                    if (op2eax) {
                        MOVD(RCX, RAX);
                    } else {
                        MOVD(RCX, GETOP(inst.op2));
                    }
                    LABEL(lnormal);
                    LABEL(lend);
                    CMPB(RCX, 32);
                    JB(lnormal);
                    SARD(dest, 31);
                    JMP(lend);
                    L(lnormal);
                    SARCD(dest);
                    L(lend);
                }
                break;
            }
            case IR_ROR: {
                bool op2eax = false;
                if (!inst.imm2 && SAMEREG(inst.op2, i)) {
                    MOVD(RAX, GETOP(inst.op2));
                    op2eax = true;
                }
                auto dest = GETOP(i);
                if (inst.imm1) {
                    MOVD(dest, inst.op1);
                } else if (!SAMEREG(inst.op1, i)) {
                    OP(MOVD, dest, GETOP(inst.op1));
                }
                if (inst.imm2) {
                    RORD(dest, inst.op2);
                } else {
                    if (op2eax) {
                        MOVD(RCX, RAX);
                    } else {
                        MOVD(RCX, GETOP(inst.op2));
                    }
                    RORCD(dest);
                }
                break;
            }
            case IR_RRC: {
                auto dst = GETOP(i);
                if (inst.imm1) {
                    MOVD(dst, inst.op1);
                } else if (!SAMEREG(inst.op1, i)) {
                    OP(MOVD, dst, GETOP(inst.op1));
                }
                RCRD(dst, 1);
                break;
            }
            case IR_ADD:
                BINARY(ADDD);
                break;
            case IR_SUB:
                BINARY(SUBD);
                if (ir->code.d[i + 1].opcode == IR_GETC) CMC();
                break;
            case IR_ADC:
                BINARY(ADCD);
                break;
            case IR_SBC:
                CMC();
                BINARY(SBBD);
                if (ir->code.d[i + 1].opcode == IR_GETC) CMC();
                break;
            case IR_MUL: {
                IRInstr hinst = ir->code.d[i + 1];
                if (hinst.opcode == IR_SMULH || hinst.opcode == IR_UMULH) {
                    if (inst.imm1) {
                        MOVD(RAX, inst.op1);
                    } else {
                        MOVD(RAX, GETOP(inst.op1));
                    }
                    auto msrc = inst.imm2 ? MKOP(RDX) : GETOP(inst.op2);
                    if (inst.imm2) MOVD(RDX, inst.op2);
                    if (hinst.opcode == IR_SMULH) {
                        IMULD(msrc);
                    } else {
                        MULD(msrc);
                    }
                    MOVD(GETOP(i), RAX);
                    MOVD(GETOP(i + 1), RDX);
                    i++;
                } else {
                    auto dest = GETOP(i);
                    if (inst.imm2) {
                        if (inst.imm1) {
                            MOVD(dest, inst.op1 * inst.op2);
                        } else {
                            auto mdst = dest.isMem ? RDX : dest.r;
                            IMULD(mdst, GETOP(inst.op1), inst.op2);
                            if (dest.isMem) MOVD(dest, mdst);
                        }
                    } else {
                        bool op2eax = false;
                        if (!dest.isMem && SAMEREG(i, inst.op2)) {
                            MOVD(RAX, GETOP(inst.op2));
                            op2eax = true;
                        }
                        auto mdst = dest.isMem ? RDX : dest.r;
                        if (inst.imm1) {
                            MOVD(mdst, inst.op1);
                        } else {
                            MOVD(mdst, GETOP(inst.op1));
                        }
                        if (op2eax) {
                            IMULD(mdst, RAX);
                        } else {
                            IMULD(mdst, GETOP(inst.op2));
                        }
                        if (dest.isMem) MOVD(dest, mdst);
                    }
                }
                break;
            }
            case IR_SMULH:
            case IR_UMULH: {
                if (inst.imm1) {
                    MOVD(RAX, inst.op1);
                } else {
                    MOVD(RAX, GETOP(inst.op1));
                }
                auto msrc = inst.imm2 ? MKOP(RDX) : GETOP(inst.op2);
                if (inst.imm2) MOVD(RDX, inst.op2);
                if (inst.opcode == IR_SMULH) {
                    IMULD(msrc);
                } else {
                    MULD(msrc);
                }
                MOVD(GETOP(i), RDX);
                break;
            }
            case IR_SMULW: {
                if (inst.imm1) {
                    MOVD(RAX, inst.op1);
                    MOVSXD(RAX, RAX);
                } else {
                    MOVSXD(RAX, GETOP(inst.op1));
                }
                if (inst.imm2) {
                    MOVD(RDX, inst.op2);
                    MOVSXD(RDX, RDX);
                } else {
                    MOVSXD(RDX, GETOP(inst.op2));
                }
                IMULQ(RAX, RDX);
                SARQ(RAX, 16);
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_CLZ: {
                auto dest = GETOP(i);
                if (inst.imm2) {
                    MOVD(RDX, inst.op2);
                    LZCNTD(RDX, RDX);
                    MOVD(dest, RDX);
                } else {
                    if (dest.isMem) {
                        LZCNTD(RDX, GETOP(inst.op2));
                        MOVD(dest, RDX);
                    } else {
                        LZCNTD(dest.r, GETOP(inst.op2));
                    }
                }
                break;
            }
            case IR_REV: {
                if (inst.imm2) {
                    MOVD(RDX, inst.op2);
                } else {
                    MOVD(RDX, GETOP(inst.op2));
                }
                BSWAPD(RDX);
                MOVD(GETOP(i), RDX);
                break;
            }
            case IR_REV16: {
                if (inst.imm2) {
                    MOVD(RDX, inst.op2);
                } else {
                    MOVD(RDX, GETOP(inst.op2));
                }
                BSWAPD(RDX);
                RORD(RDX, 16);
                MOVD(GETOP(i), RDX);
                break;
            }
            case IR_USAT: {
                if (inst.imm2) {
                    MOVD(RDX, inst.op2);
                } else {
                    MOVD(RDX, GETOP(inst.op2));
                }
                MOVD(RCX, 0);
                CMPD(RDX, RCX);
                CMOVLD(RDX, RCX);
                MOVD(RCX, MASK(inst.op1));
                CMPD(RDX, RCX);
                CMOVGD(RDX, RCX);
                MOVD(GETOP(i), RDX);
                break;
            }
            case IR_SSAT: {
                if (inst.imm2) {
                    MOVD(RDX, inst.op2);
                } else {
                    MOVD(RDX, GETOP(inst.op2));
                }
                MOVD(RCX, ~MASK(inst.op1));
                CMPD(RDX, RCX);
                CMOVLD(RDX, RCX);
                MOVD(RCX, MASK(inst.op1));
                CMPD(RDX, RCX);
                CMOVGD(RDX, RCX);
                MOVD(GETOP(i), RDX);
                break;
            }
            case IR_MEDIA_UADD8: {
                if (inst.imm2) {
                    MOVD(ARG3, inst.op2);
                } else {
                    MOVD(ARG3, GETOP(inst.op2));
                }
                if (inst.imm1) {
                    MOVD(ARG2, inst.op1);
                } else {
                    MOVD(ARG2, GETOP(inst.op1));
                }
                MOVQ(ARG1, RBX);
                MOVQ(RAX, (u64) media_uadd8);
                CALL(RAX);
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_MEDIA_USUB8: {
                if (inst.imm2) {
                    MOVD(ARG3, inst.op2);
                } else {
                    MOVD(ARG3, GETOP(inst.op2));
                }
                if (inst.imm1) {
                    MOVD(ARG2, inst.op1);
                } else {
                    MOVD(ARG2, GETOP(inst.op1));
                }
                MOVQ(ARG1, RBX);
                MOVQ(RAX, (u64) media_usub8);
                CALL(RAX);
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_MEDIA_UQADD8: {
                if (inst.imm1) {
                    MOVD(RDX, inst.op1);
                } else {
                    MOVD(RDX, GETOP(inst.op1));
                }
                MOVD(XMM0, RDX);
                if (inst.imm2) {
                    MOVD(RDX, inst.op2);
                } else {
                    MOVD(RDX, GETOP(inst.op2));
                }
                MOVD(XMM1, RDX);
                PADDUSB(XMM0, XMM1);
                MOVD(RAX, XMM0);
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_MEDIA_UQSUB8: {
                if (inst.imm1) {
                    MOVD(RDX, inst.op1);
                } else {
                    MOVD(RDX, GETOP(inst.op1));
                }
                MOVD(XMM0, RDX);
                if (inst.imm2) {
                    MOVD(RDX, inst.op2);
                } else {
                    MOVD(RDX, GETOP(inst.op2));
                }
                MOVD(XMM1, RDX);
                PSUBUSB(XMM0, XMM1);
                MOVD(RAX, XMM0);
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_MEDIA_UHADD8: {
                if (inst.imm2) {
                    MOVD(ARG3, inst.op2);
                } else {
                    MOVD(ARG3, GETOP(inst.op2));
                }
                if (inst.imm1) {
                    MOVD(ARG2, inst.op1);
                } else {
                    MOVD(ARG2, GETOP(inst.op1));
                }
                MOVQ(ARG1, RBX);
                MOVQ(RAX, (u64) media_uhadd8);
                CALL(RAX);
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_MEDIA_SSUB8: {
                if (inst.imm2) {
                    MOVD(ARG3, inst.op2);
                } else {
                    MOVD(ARG3, GETOP(inst.op2));
                }
                if (inst.imm1) {
                    MOVD(ARG2, inst.op1);
                } else {
                    MOVD(ARG2, GETOP(inst.op1));
                }
                MOVQ(ARG1, RBX);
                MOVQ(RAX, (u64) media_ssub8);
                CALL(RAX);
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_MEDIA_QSUB8: {
                if (inst.imm1) {
                    MOVD(RDX, inst.op1);
                } else {
                    MOVD(RDX, GETOP(inst.op1));
                }
                MOVD(XMM0, RDX);
                if (inst.imm2) {
                    MOVD(RDX, inst.op2);
                } else {
                    MOVD(RDX, GETOP(inst.op2));
                }
                MOVD(XMM1, RDX);
                PSUBSB(XMM0, XMM1);
                MOVD(RAX, XMM0);
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_MEDIA_SEL: {
                if (inst.imm2) {
                    MOVD(ARG3, inst.op2);
                } else {
                    MOVD(ARG3, GETOP(inst.op2));
                }
                if (inst.imm1) {
                    MOVD(ARG2, inst.op1);
                } else {
                    MOVD(ARG2, GETOP(inst.op1));
                }
                MOVQ(ARG1, RBX);
                MOVQ(RAX, (u64) media_sel);
                CALL(RAX);
                MOVD(GETOP(i), RAX);
                break;
            }
            case IR_GETN: {
                auto dest = GETOP(i);
                if (inst.imm2) {
                    MOVD(dest, inst.op2 >> 31);
                } else {
                    if (lastflags != inst.op2) {
                        auto src = GETOP(inst.op2);
                        if (src.isMem) {
                            CMPD(src, 0);
                        } else {
                            TESTD(src.r, src.r);
                        }
                        LAHF();
                        lastflags = inst.op2;
                    }
                    if (dest.isMem) {
                        XORD(RDX, RDX);
                        TESTB(AH, BIT(7));
                        SETNZ(RDX);
                        MOVD(dest, RDX);
                    } else {
                        XORD(dest.r, dest.r);
                        TESTB(AH, BIT(7));
                        SETNZ(dest.r);
                    }
                }
                break;
            }
            case IR_GETZ: {
                auto dest = GETOP(i);
                if (inst.imm2) {
                    MOVD(dest, inst.op2 == 0);
                } else {
                    if (lastflags != inst.op2) {
                        auto src = GETOP(inst.op2);
                        if (src.isMem) {
                            CMPD(src, 0);
                        } else {
                            TESTD(src.r, src.r);
                        }
                        LAHF();
                        lastflags = inst.op2;
                    }
                    if (dest.isMem) {
                        XORD(RDX, RDX);
                        TESTB(AH, BIT(6));
                        SETNZ(RDX);
                        MOVD(dest, RDX);
                    } else {
                        XORD(dest.r, dest.r);
                        TESTB(AH, BIT(6));
                        SETNZ(dest.r);
                    }
                }
                break;
            }
            case IR_GETC: {
                if (lastflags != inst.op2) {
                    LAHF();
                    SETO(RAX);
                    lastflags = inst.op2;
                }
                auto dest = GETOP(i);
                if (dest.isMem) {
                    XORD(RDX, RDX);
                    TESTB(AH, BIT(0));
                    SETNZ(RDX);
                    MOVD(dest, RDX);
                } else {
                    XORD(dest.r, dest.r);
                    TESTB(AH, BIT(0));
                    SETNZ(dest.r);
                }
                break;
            }
            case IR_SETC: {
                if (inst.imm2) {
                    if (inst.op2) STC();
                    else CLC();
                } else {
                    MOVD(RDX, GETOP(inst.op2));
                    SHRD(RDX, 1);
                }
                break;
            }
            case IR_GETCIFZ: {
                if (inst.imm1) {
                    if (inst.op1) {
                        if (inst.imm2) {
                            MOVD(GETOP(i), inst.op2);
                        } else {
                            OP(MOVD, GETOP(i), GETOP(inst.op2));
                        }
                    } else {
                        auto dest = GETOP(i);
                        SETC(RDX);
                        MOVZXBD(RDX, RDX);
                        MOVD(dest, RDX);
                    }
                } else {
                    SETC(RDX);
                    MOVZXBD(RDX, RDX);
                    auto op1 = GETOP(inst.op1);
                    if (op1.isMem) {
                        CMPD(op1, 0);
                    } else {
                        TESTD(op1.r, op1.r);
                    }
                    if (inst.imm2) {
                        MOVD(RCX, inst.op2);
                        CMOVNED(RDX, RCX);
                    } else {
                        CMOVNED(RDX, GETOP(inst.op2));
                    }
                    MOVD(GETOP(i), RDX);
                }
                break;
            }
            case IR_GETV: {
                if (lastflags != inst.op2) {
                    LAHF();
                    SETO(RAX);
                    lastflags = inst.op2;
                }
                auto dest = GETOP(i);
                if (dest.isMem) {
                    MOVZXBD(RDX, RAX);
                    MOVD(dest, RDX);
                } else {
                    MOVZXBD(dest.r, RAX);
                }
                break;
            }
            case IR_PCMASK: {
                if (inst.imm1) {
                    MOVD(GETOP(i), inst.op1 ? ~1 : ~3);
                } else {
                    auto op1 = GETOP(inst.op1);
                    auto dest = GETOP(i);
                    OP(MOVD, dest, op1);
                    SHLD(dest, 1);
                    SUBD(dest, 4);
                }
                break;
            }
            case IR_JZ: {
                jmptarget = inst.op2;
                labels[nlabel++] = LNEW();
                if (inst.imm1) {
                    if (!inst.op1) JMP(labels[nlabel - 1], NEAR);
                } else {
                    auto src = GETOP(inst.op1);
                    if (src.isMem) {
                        CMPD(src, 0);
                    } else {
                        TESTD(src.r, src.r);
                    }
                    JZ(labels[nlabel - 1], NEAR);
                }
                break;
            }
            case IR_JNZ: {
                jmptarget = inst.op2;
                labels[nlabel++] = LNEW();
                if (inst.imm1) {
                    if (!inst.op1) JMP(labels[nlabel - 1], NEAR);
                } else {
                    auto src = GETOP(inst.op1);
                    if (src.isMem) {
                        CMPD(src, 0);
                    } else {
                        TESTD(src.r, src.r);
                    }
                    JNZ(labels[nlabel - 1], NEAR);
                }
                break;
            }
            case IR_JELSE: {
                jmptarget = inst.op2;
                labels[nlabel++] = LNEW();
                JMP(labels[nlabel - 1], NEAR);
                L(labels[nlabel - 2]);
                break;
            }
            case IR_MODESWITCH: {
                MOVQ(ARG1, RBX);
                MOVD(ARG2, inst.op1);
                MOVQ(RAX, (u64) cpu_update_mode);
                CALL(RAX);
                break;
            }
            case IR_EXCEPTION: {
                MOVQ(ARG1, RBX);
                switch (inst.op1) {
                    case E_SWI:
                        MOVD(ARG2, (ArmInstr) {inst.op2}.sw_intr.arg);
                        MOVQ(RAX, (u64) cpu->handle_svc);
                        CALL(RAX);
                        break;
                    case E_UND:
                        MOVD(ARG2, inst.op2);
                        MOVQ(RAX, (u64) cpu_undefined_fail);
                        CALL(RAX);
                        break;
                }
                break;
            }
            case IR_HALT: {
                MOVB(CPU(halt), 1);
                break;
            }
            case IR_BEGIN: {
                PUSH(RBX);
                for (u32 i = 0; i < this->hralloc.count[REG_SAVED]; i++) {
                    PUSH(savedregs[i]);
                }
                int spdisp = getSPDisp(this);
                if (spdisp) SUBQ(RSP, spdisp);
                MOVQ(RBX, (u64) cpu);
                L(looplabel);

                break;
            }
            case IR_END_RET:
            case IR_END_LINK:
            case IR_END_LOOP: {

                MOVQ(RAX, CPU(cycles));
                SUBQ(RAX, inst.cycles);
                MOVQ(CPU(cycles), RAX);

                if (inst.opcode == IR_END_LOOP) {
                    CMPQ(RAX, 0);
                    JG(looplabel, NEAR);
                }

                int spdisp = getSPDisp(this);
                if (spdisp) ADDQ(RSP, spdisp);
                for (int i = this->hralloc.count[REG_SAVED] - 1; i >= 0; i--) {
                    POP(savedregs[i]);
                }
                POP(RBX);

                if (inst.opcode == IR_END_LINK) {
                    LABEL(lnolink);
                    LABEL(linkaddr);
                    CMPQ(RAX, 0);
                    JLE(lnolink);
                    Vec_push(this->links,
                             ((X86LinkPatch) {linkaddr, inst.op1, inst.op2}));
                    BYTE(0x48);
                    BYTE(0xb8);
                    QUAD(linkaddr);
                    JMP(RAX);
                    L(lnolink);
                }
                RET();
                break;
            }
            default:
                break;
        }
    }
    return this;
}

static void compileVFPDataProc(X86CodeBackend* this, ArmInstr instr) {
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
                    MOVSD(XMM0, CPU(d[vd]));
                    MOVSD(XMM1, CPU(d[vn]));
                    MULSD(XMM1, CPU(d[vm]));
                    SUBSD(XMM0, XMM1);
                    MOVSD(CPU(d[vd]), XMM0);
                } else {
                    MOVSS(XMM0, CPU(s[vd]));
                    MOVSS(XMM1, CPU(s[vn]));
                    MULSS(XMM1, CPU(s[vm]));
                    SUBSS(XMM0, XMM1);
                    MOVSS(CPU(s[vd]), XMM0);
                }
            } else {
                if (dp) {
                    MOVSD(XMM0, CPU(d[vn]));
                    MULSD(XMM0, CPU(d[vm]));
                    ADDSD(XMM0, CPU(d[vd]));
                    MOVSD(CPU(d[vd]), XMM0);
                } else {
                    MOVSS(XMM0, CPU(s[vn]));
                    MULSS(XMM0, CPU(s[vm]));
                    ADDSS(XMM0, CPU(s[vd]));
                    MOVSS(CPU(s[vd]), XMM0);
                }
            }
            break;
        case 1:
            if (op) {
                if (dp) {
                    MOVSD(XMM0, CPU(d[vn]));
                    MULSD(XMM0, CPU(d[vm]));
                    ADDSD(XMM0, CPU(d[vd]));
                    XORPD(XMM1, XMM1);
                    SUBSD(XMM1, XMM0);
                    MOVSD(CPU(d[vd]), XMM1);
                } else {
                    MOVSS(XMM0, CPU(s[vn]));
                    MULSS(XMM0, CPU(s[vm]));
                    ADDSS(XMM0, CPU(s[vd]));
                    XORPS(XMM1, XMM1);
                    SUBSS(XMM1, XMM0);
                    MOVSS(CPU(s[vd]), XMM1);
                }
            } else {
                if (dp) {
                    MOVSD(XMM0, CPU(d[vn]));
                    MULSD(XMM0, CPU(d[vm]));
                    SUBSD(XMM0, CPU(d[vd]));
                    MOVSD(CPU(d[vd]), XMM0);
                } else {
                    MOVSS(XMM0, CPU(s[vn]));
                    MULSS(XMM0, CPU(s[vm]));
                    SUBSS(XMM0, CPU(s[vd]));
                    MOVSS(CPU(s[vd]), XMM0);
                }
            }
            break;
        case 2:
            if (dp) {
                MOVSD(XMM0, CPU(d[vn]));
                MULSD(XMM0, CPU(d[vm]));
                if (op) {
                    XORPD(XMM1, XMM1);
                    SUBSD(XMM1, XMM0);
                    MOVSD(CPU(d[vd]), XMM1);
                } else {
                    MOVSD(CPU(d[vd]), XMM0);
                }
            } else {
                MOVSS(XMM0, CPU(s[vn]));
                MULSS(XMM0, CPU(s[vm]));
                if (op) {
                    XORPS(XMM1, XMM1);
                    SUBSS(XMM1, XMM0);
                    MOVSS(CPU(s[vd]), XMM1);
                } else {
                    MOVSS(CPU(s[vd]), XMM0);
                }
            }
            break;
        case 3:
            if (op) {
                if (dp) {
                    MOVSD(XMM0, CPU(d[vn]));
                    SUBSD(XMM0, CPU(d[vm]));
                    MOVSD(CPU(d[vd]), XMM0);
                } else {
                    MOVSS(XMM0, CPU(s[vn]));
                    SUBSS(XMM0, CPU(s[vm]));
                    MOVSS(CPU(s[vd]), XMM0);
                }
            } else {
                if (dp) {
                    MOVSD(XMM0, CPU(d[vn]));
                    ADDSD(XMM0, CPU(d[vm]));
                    MOVSD(CPU(d[vd]), XMM0);
                } else {
                    MOVSS(XMM0, CPU(s[vn]));
                    ADDSS(XMM0, CPU(s[vm]));
                    MOVSS(CPU(s[vd]), XMM0);
                }
            }
            break;
        case 8:
            if (dp) {
                MOVSD(XMM0, CPU(d[vn]));
                DIVSD(XMM0, CPU(d[vm]));
                MOVSD(CPU(d[vd]), XMM0);
            } else {
                MOVSS(XMM0, CPU(s[vn]));
                DIVSS(XMM0, CPU(s[vm]));
                MOVSS(CPU(s[vd]), XMM0);
            }
            break;
        case 11: {
            op = instr.cp_data_proc.cp & 4;
            switch (instr.cp_data_proc.crn) {
                case 0:
                    if (op) {
                        if (dp) {
                            MOVQ(RDX, MASKL(63));
                            ANDQ(RDX, CPU(d[vm]));
                            MOVQ(CPU(d[vd]), RDX);
                        } else {
                            MOVD(RDX, CPU(s[vm]));
                            ANDD(RDX, MASK(31));
                            MOVD(CPU(s[vd]), RDX);
                        }
                    } else {
                        if (dp) {
                            MOVSD(XMM0, CPU(d[vm]));
                            MOVSD(CPU(d[vd]), XMM0);
                        } else {
                            MOVSS(XMM0, CPU(s[vm]));
                            MOVSS(CPU(s[vd]), XMM0);
                        }
                    }
                    break;
                case 1:
                    if (op) {
                        if (dp) {
                            SQRTSD(XMM0, CPU(d[vm]));
                            MOVSD(CPU(d[vd]), XMM0);
                        } else {
                            SQRTSS(XMM0, CPU(s[vm]));
                            MOVSS(CPU(s[vd]), XMM0);
                        }
                    } else {
                        if (dp) {
                            MOVQ(RDX, BITL(63));
                            XORQ(RDX, CPU(d[vm]));
                            MOVQ(CPU(d[vd]), RDX);
                        } else {
                            MOVD(RDX, CPU(s[vm]));
                            XORD(RDX, BIT(31));
                            MOVD(CPU(s[vd]), RDX);
                        }
                    }
                    break;
                case 4:
                case 5:
                    if (dp) {
                        MOVSD(XMM0, CPU(d[vd]));
                        if (instr.cp_data_proc.crn & 1) {
                            XORPD(XMM1, XMM1);
                            COMISD(XMM0, XMM1);
                        } else {
                            COMISD(XMM0, CPU(d[vm]));
                        }
                    } else {
                        MOVSS(XMM0, CPU(s[vd]));
                        if (instr.cp_data_proc.crn & 1) {
                            XORPS(XMM1, XMM1);
                            COMISS(XMM0, XMM1);
                        } else {
                            COMISS(XMM0, CPU(s[vm]));
                        }
                    }
                    // x86 flags to arm flags
                    // 00 -> 0010 (gt)
                    // 01 -> 1000 (lt)
                    // 10 -> 0110 (eq)
                    // 11 -> 0011 (uo)
                    LABEL(l0);
                    LABEL(l1);
                    LABEL(l2);
                    LABEL(lend);
                    LAHF();
                    ANDD(CPU(fpscr), 0x0fff'ffff);
                    ANDB(AH, 0x41);
                    JZ(l0);
                    CMPB(AH, 0x01);
                    JZ(l1);
                    CMPB(AH, 0x40);
                    JZ(l2);
                    ORD(CPU(fpscr), 3 << 28);
                    JMP(lend);
                    L(l2);
                    ORD(CPU(fpscr), 6 << 28);
                    JMP(lend);
                    L(l1);
                    ORD(CPU(fpscr), 8 << 28);
                    JMP(lend);
                    L(l0);
                    ORD(CPU(fpscr), 2 << 28);
                    L(lend);
                    break;
                case 7:
                    if (dp) {
                        vd = vd << 1 | ((instr.cp_data_proc.cpopc >> 2) & 1);

                        CVTSD2SS(XMM0, CPU(d[vm]));
                        MOVSS(CPU(s[vd]), XMM0);
                    } else {
                        vd = vd >> 1;

                        CVTSS2SD(XMM0, CPU(s[vm]));
                        MOVSD(CPU(d[vd]), XMM0);
                    }
                    break;
                case 8:
                    if (dp) {
                        vm = vm << 1 | (instr.cp_data_proc.cp & 1);

                        if (op) {
                            CVTSI2SDD(XMM0, CPU(s[vm]));
                            MOVSD(CPU(d[vd]), XMM0);
                        } else {
                            MOVD(RDX, CPU(s[vm]));
                            CVTSI2SDQ(XMM0, RDX);
                            MOVSD(CPU(d[vd]), XMM0);
                        }
                    } else {
                        if (op) {
                            CVTSI2SSD(XMM0, CPU(s[vm]));
                            MOVSS(CPU(s[vd]), XMM0);
                        } else {
                            MOVD(RDX, CPU(s[vm]));
                            CVTSI2SSQ(XMM0, RDX);
                            MOVSS(CPU(s[vd]), XMM0);
                        }
                    }
                    break;
                case 12:
                case 13: {
                    LABEL(lnooverflow);
                    if (dp) {
                        vd = vd << 1 | ((instr.cp_data_proc.cpopc >> 2) & 1);

                        if (instr.cp_data_proc.crn & 1) {
                            CVTTSD2SID(RDX, CPU(d[vm]));
                            CMPD(RDX, BIT(31));
                            JNZ(lnooverflow);
                            MOVD(RDX, MASK(31));
                            L(lnooverflow);
                            MOVD(CPU(s[vd]), RDX);
                        } else {
                            CVTTSD2SIQ(RDX, CPU(d[vm]));
                            MOVQ(RAX, BITL(63));
                            CMPQ(RDX, RAX);
                            JNZ(lnooverflow);
                            MOVD(RDX, MASKL(32));
                            L(lnooverflow);
                            MOVD(CPU(s[vd]), RDX);
                        }
                    } else {
                        if (instr.cp_data_proc.crn & 1) {
                            CVTTSS2SID(RDX, CPU(s[vm]));
                            CMPD(RDX, BIT(31));
                            JNZ(lnooverflow);
                            MOVD(RDX, MASK(31));
                            L(lnooverflow);
                            MOVD(CPU(s[vd]), RDX);
                        } else {
                            CVTTSS2SIQ(RDX, CPU(s[vm]));
                            MOVQ(RAX, BITL(63));
                            CMPQ(RDX, RAX);
                            JNZ(lnooverflow);
                            MOVD(RDX, MASKL(32));
                            L(lnooverflow);
                            MOVD(CPU(s[vd]), RDX);
                        }
                    }
                    break;
                }
            }
            break;
        }
    }
}

static void compileVFPRead(X86CodeBackend* this, ArmInstr instr, rasX64Op dst) {
    if (instr.cp_reg_trans.cpopc == 7) {
        if (instr.cp_reg_trans.crn == 1) {
            OP(MOVD, dst, CPU(fpscr));
        } else {
            lwarn("unknown vfp special reg %d", instr.cp_reg_trans.crn);
            MOVD(dst, 0);
        }
        return;
    }

    u32 vn = instr.cp_reg_trans.crn << 1;
    if (instr.cp_reg_trans.cpnum & 1) vn |= instr.cp_reg_trans.cpopc & 1;
    else vn |= instr.cp_reg_trans.cp >> 2;

    OP(MOVD, dst, CPU(s[vn]));
}

static void compileVFPWrite(X86CodeBackend* this, ArmInstr instr,
                            rasX64Op src) {
    if (instr.cp_reg_trans.cpopc == 7) {
        if (instr.cp_reg_trans.crn == 1) {
            OP(MOVD, CPU(fpscr), src);
        } else {
            lwarn("unknown vfp special reg %d", instr.cp_reg_trans.crn);
        }
        return;
    }

    u32 vn = instr.cp_reg_trans.crn << 1;
    if (instr.cp_reg_trans.cpnum & 1) vn |= instr.cp_reg_trans.cpopc & 1;
    else vn |= instr.cp_reg_trans.cp >> 2;

    OP(MOVD, CPU(s[vn]), src);
}

static void compileVFPRead64(X86CodeBackend* this, ArmInstr instr, rasX64Op dst,
                             bool hi) {
    if (instr.cp_double_reg_trans.cpnum & 1) {
        u32 vm = instr.cp_double_reg_trans.crm;
        if (hi) {
            OP(MOVD, dst, CPU(d[vm], 4));
        } else {
            OP(MOVD, dst, CPU(d[vm]));
        }
    } else {
        u32 vm = instr.cp_double_reg_trans.crm << 1 |
                 ((instr.cp_double_reg_trans.cp >> 1) & 1);
        if (hi) {
            OP(MOVD, dst, CPU(s[vm + 1]));
        } else {
            OP(MOVD, dst, CPU(s[vm]));
        }
    }
}

static void compileVFPWrite64(X86CodeBackend* this, ArmInstr instr,
                              const rasX64Op src, bool hi) {
    if (instr.cp_double_reg_trans.cpnum & 1) {
        u32 vm = instr.cp_double_reg_trans.crm;
        if (hi) {
            OP(MOVD, CPU(d[vm], 4), src);
        } else {
            OP(MOVD, CPU(d[vm]), src);
        }
    } else {
        u32 vm = instr.cp_double_reg_trans.crm << 1 |
                 ((instr.cp_double_reg_trans.cp >> 1) & 1);
        if (hi) {
            if (vm < 31) OP(MOVD, CPU(s[vm + 1]), src);
        } else {
            OP(MOVD, CPU(s[vm]), src);
        }
    }
}

static void compileVFPLoadMem(X86CodeBackend* this, ArmInstr instr,
                              rasX64Op addr) {
    u32 rcount;
    if (instr.cp_data_trans.p && !instr.cp_data_trans.w) {
        rcount = 1;
    } else {
        rcount = instr.cp_data_trans.offset;
        if (instr.cp_data_trans.cpnum & 1) rcount >>= 1;
    }

    u32 vd = instr.cp_data_trans.crd;

    if (rcount > 1) {
        // need to set ebp with addr before modifying rsp
        MOVQ(PTR(-8, RSP), RBP);
        MOVD(RBP, addr);
        SUBQ(RSP, 16);
        addr = MKOP(RBP);
    }

    if (instr.cp_data_trans.cpnum & 1) {
        for (int i = 0; i < rcount; i++) {
#ifdef JIT_FASTMEM
            MOVQ(RAX, (u64) this->cpu->fastmem);
            if (addr.isMem || addr.r.idx != RDX.idx) MOVD(RDX, addr);
            MOVSD(XMM0, PTR(RAX, RDX));
#else
            MOVD(ARG2, addr);
            MOVQ(ARG1, RBX);
            MOVQ(RAX, (u64) this->cpu->readf64);
            CALL(RAX);
#endif
            MOVSD(CPU(d[(vd + i) & 15]), XMM0);
            if (i < rcount - 1) ADDD(addr, 8);
        }
    } else {
        vd = vd << 1 | instr.cp_data_trans.n;

        for (int i = 0; i < rcount; i++) {
#ifdef JIT_FASTMEM
            MOVQ(RAX, (u64) this->cpu->fastmem);
            if (addr.isMem || addr.r.idx != RDX.idx) MOVD(RDX, addr);
            MOVSS(XMM0, PTR(RAX, RDX));
#else
            MOVD(ARG2, addr);
            MOVQ(ARG1, RBX);
            MOVQ(RAX, (u64) this->cpu->readf32);
            CALL(RAX);
#endif
            MOVSS(CPU(s[(vd + i) & 31]), XMM0);
            if (i < rcount - 1) ADDD(addr, 4);
        }
    }

    if (rcount > 1) {
        ADDQ(RSP, 8);
        POP(RBP);
    }
}

static void compileVFPStoreMem(X86CodeBackend* this, ArmInstr instr,
                               rasX64Op addr) {
    u32 rcount;
    if (instr.cp_data_trans.p && !instr.cp_data_trans.w) {
        rcount = 1;
    } else {
        rcount = instr.cp_data_trans.offset;
        if (instr.cp_data_trans.cpnum & 1) rcount >>= 1;
    }

    u32 vd = instr.cp_data_trans.crd;

    if (rcount > 1) {
        // need to set ebp with addr before modifying rsp
        MOVQ(PTR(-8, RSP), RBP);
        MOVD(RBP, addr);
        SUBQ(RSP, 16);
        addr = MKOP(RBP);
    }

    if (instr.cp_data_trans.cpnum & 1) {
        for (int i = 0; i < rcount; i++) {
            MOVSD(ARG3F, CPU(d[(vd + i) & 15]));
#ifdef JIT_FASTMEM
            MOVQ(RAX, (u64) this->cpu->fastmem);
            if (addr.isMem || addr.r.idx != RDX.idx) MOVD(RDX, addr);
            MOVSD(PTR(RAX, RDX), ARG3F);
#else
            MOVD(ARG2, addr);
            MOVQ(ARG1, RBX);
            MOVQ(RAX, (u64) this->cpu->writef64);
            CALL(RAX);
#endif
            if (i < rcount - 1) ADDD(addr, 8);
        }
    } else {
        vd = vd << 1 | instr.cp_data_trans.n;

        for (int i = 0; i < rcount; i++) {
            MOVSS(ARG3F, CPU(s[(vd + i) & 31]));
#ifdef JIT_FASTMEM
            MOVQ(RAX, (u64) this->cpu->fastmem);
            if (addr.isMem || addr.r.idx != RDX.idx) MOVD(RDX, addr);
            MOVSS(PTR(RAX, RDX), ARG3F);
#else
            MOVD(ARG2, addr);
            MOVQ(ARG1, RBX);
            MOVQ(RAX, (u64) this->cpu->writef32);
            CALL(RAX);
#endif
            if (i < rcount - 1) ADDD(addr, 4);
        }
    }

    if (rcount > 1) {
        ADDQ(RSP, 8);
        POP(RBP);
    }
}

JITFunc backend_x86_get_code(X86CodeBackend* backend) {
    return rasGetCode(backend->code);
}

void backend_x86_patch_links(JITBlock* block) {
    auto* code = (X86CodeBackend*) block->backend;
    Vec_foreach(p, code->links) {
        JITBlock* linkblock = get_jitblock(code->cpu, p->attrs, p->addr);
        rasDefineLabelExternal(p->lab, linkblock->code);
        Vec_push(linkblock->linkingblocks,
                 ((BlockLocation) {block->attrs, block->start_addr}));
    }

    rasReady(code->code);
}

void backend_x86_free(X86CodeBackend* backend) {
    hostregalloc_free(&backend->hralloc);
    Vec_free(backend->links);
    rasDestroy(backend->code);
    free(backend);
}

#ifndef NOCAPSTONE
void backend_x86_disassemble(X86CodeBackend* backend) {
    print_hostregs(backend);
    csh handle;
    cs_insn* insn;
    cs_open(CS_ARCH_X86, CS_MODE_64, &handle);
    cs_option(handle, CS_OPT_SKIPDATA, CS_OPT_ON);
    size_t count = cs_disasm(handle, rasGetCode(backend->code),
                             rasGetSize(backend->code), 0, 0, &insn);
    printf("--------- JIT Disassembly at %p ------------\n",
           rasGetCode(backend->code));
    for (size_t i = 0; i < count; i++) {
        printf("%04lx: %s %s\n", insn[i].address, insn[i].mnemonic,
               insn[i].op_str);
    }
    cs_free(insn, count);
}
#endif

#endif
