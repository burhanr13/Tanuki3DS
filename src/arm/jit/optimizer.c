#include "optimizer.h"

#include <string.h>

#include "jit.h"

#define MOVX(_op2, imm)                                                        \
    ((IRInstr){.opcode = IR_MOV, .imm1 = 1, .imm2 = imm, .op1 = 0, .op2 = _op2})
#define MOVI(op2) MOVX(op2, 1)
#define NOP                                                                    \
    ((IRInstr){.opcode = IR_NOP, .imm1 = 1, .imm2 = 1, .op1 = 0, .op2 = 0})

void optimize_loadstore(IRBlock* block) {
    u32 vreg[16] = {};
    bool immreg[16] = {};
    u32 laststorereg[16] = {};
    u32 vflag[5] = {};
    bool immflag[5] = {};
    u32 laststoreflag[5] = {};

    u32 jmpsource = 0;
    u32 jmptarget = -1;
    for (int i = 0; i < block->code.size; i++) {
        IRInstr inst = block->code.d[i];
        if (i == jmptarget) {
            for (int r = 0; r < 16; r++) {
                if (laststorereg[r] > jmpsource ||
                    (vreg[r] > jmpsource && !immreg[r])) {
                    laststorereg[r] = 0;
                    vreg[r] = 0;
                    immreg[r] = false;
                }
            }
            for (int f = 0; f < 5; f++) {
                if (laststoreflag[f] > jmpsource ||
                    (vflag[f] > jmpsource && !immflag[f])) {
                    laststoreflag[f] = 0;
                    vflag[f] = 0;
                    immflag[f] = false;
                }
            }
            jmpsource = 0;
        }
        switch (inst.opcode) {
            case IR_LOAD_REG: {
                u32 rd = inst.op1;
                if (vreg[rd] || immreg[rd]) {
                    block->code.d[i] = MOVX(vreg[rd], immreg[rd]);
                } else {
                    vreg[rd] = i;
                    immreg[rd] = false;
                }
                break;
            }
            case IR_STORE_REG: {
                u32 rd = inst.op1;
                if (laststorereg[rd] > jmpsource) {
                    block->code.d[laststorereg[rd]] = NOP;
                }
                laststorereg[rd] = i;
                vreg[rd] = inst.op2;
                immreg[rd] = inst.imm2;
                break;
            }
            case IR_LOAD_FLAG: {
                u32 f = inst.op1;
                if (vflag[f] || immflag[f]) {
                    block->code.d[i] = MOVX(vflag[f], immflag[f]);
                } else {
                    vflag[f] = i;
                    immflag[f] = false;
                }
                break;
            }
            case IR_STORE_FLAG: {
                u32 f = inst.op1;
                if (laststoreflag[f] > jmpsource) {
                    block->code.d[laststoreflag[f]] = NOP;
                }
                laststoreflag[f] = i;
                vflag[f] = inst.op2;
                immflag[f] = inst.imm2;
                break;
            }
            case IR_LOAD_CPSR:
                for (int f = 0; f < 5; f++) {
                    laststoreflag[f] = 0;
                }
                break;
            case IR_STORE_CPSR:
                for (int f = 0; f < 5; f++) {
                    if (laststoreflag[f] > jmpsource) {
                        block->code.d[laststoreflag[f]] = NOP;
                    }
                    laststoreflag[f] = 0;
                    vflag[f] = 0;
                    immflag[f] = false;
                }
                break;
            case IR_JZ:
            case IR_JNZ:
            case IR_JELSE: {
                jmpsource = i;
                jmptarget = inst.op2;
                break;
            }
            case IR_END_RET:
            case IR_END_LINK: {
                for (int r = 0; r < 16; r++) {
                    if (laststorereg[r] > jmpsource ||
                        (vreg[r] > jmpsource && !immreg[r])) {
                        vreg[r] = 0;
                        immreg[r] = false;
                    }
                    laststorereg[r] = 0;
                }
                for (int f = 0; f < 5; f++) {
                    if (laststoreflag[f] > jmpsource ||
                        (vflag[f] > jmpsource && !immflag[f])) {
                        vflag[f] = 0;
                        immflag[f] = false;
                    }
                    laststoreflag[f] = 0;
                }
                break;
            }
            default:
                break;
        }
    }
}

#define NOOPT() (vops[i] = i, vimm[i] = false)
#define OPTV(op) (vops[i] = op, vimm[i] = false, *inst = NOP)
#define OPTI(op) (vops[i] = op, vimm[i] = true, *inst = NOP)

#define ADDCV(op1, op2, c)                                                     \
    ({                                                                         \
        u32 res = op1 + op2;                                                   \
        u32 tmpc = res < op1;                                                  \
        res += c;                                                              \
        if (inst[1].opcode == IR_GETC)                                         \
            inst[1] = MOVI(tmpc || res < op1 + op2);                           \
        if (inst[3].opcode == IR_GETV)                                         \
            inst[3] = MOVI(((op1 ^ res) & ~(op1 ^ op2)) >> 31);                \
        vops[i] = res;                                                         \
        vimm[i] = true;                                                        \
        *inst = NOP;                                                           \
    })

void constant_jmp_helper(IRBlock* block, IRInstr* jmp, bool jmpTaken) {
    for (int i = jmp->op2; i < block->code.size; i++) {
        IRInstr* next = &block->code.d[i];
        if (next->opcode != IR_NOP) {
            if (next->opcode == IR_JELSE) {
                constant_jmp_helper(block, next, !jmpTaken);
            } else {
                break;
            }
        }
    }
    if (jmpTaken) {
        IRInstr* next = &block->code.d[jmp->op2];
        for (IRInstr* i = jmp; i < next; i++) {
            *i = NOP;
        }
    } else {
        *jmp = NOP;
    }
}

void optimize_constprop(IRBlock* block) {
    u32 vops[block->code.size];
    bool vimm[block->code.size];
    bool notcurcond[block->code.size];
    u32 jmpsrc = -1;
    u32 jmptarget = -1;
    for (int i = 0; i < block->code.size; i++) {
        IRInstr* inst = &block->code.d[i];
        if (!inst->imm1) {
            u32 v = inst->op1;
            inst->op1 = vops[v];
            inst->imm1 = vimm[v];
        }
        if (!inst->imm2) {
            u32 v = inst->op2;
            inst->op2 = vops[v];
            inst->imm2 = vimm[v];
        }

        if (inst->imm1 && inst->imm2) {
            switch (inst->opcode) {
                case IR_MOV:
                    OPTI(inst->op2);
                    break;
                case IR_AND:
                    OPTI(inst->op1 & inst->op2);
                    break;
                case IR_OR:
                    OPTI(inst->op1 | inst->op2);
                    break;
                case IR_XOR:
                    OPTI(inst->op1 ^ inst->op2);
                    break;
                case IR_NOT:
                    OPTI(~inst->op2);
                    break;
                case IR_LSL:
                    if (inst->op2 >= 32) {
                        OPTI(0);
                    } else {
                        OPTI(inst->op1 << inst->op2);
                    }
                    break;
                case IR_LSR:
                    if (inst->op2 >= 32) {
                        OPTI(0);
                    } else {
                        OPTI(inst->op1 >> inst->op2);
                    }
                    break;
                case IR_ASR:
                    if (inst->op2 >= 32) {
                        OPTI((s32) inst->op1 >> 31);
                    } else {
                        OPTI((s32) inst->op1 >> inst->op2);
                    }
                    break;
                case IR_ROR:
                    inst->op2 &= 31;
                    OPTI((inst->op1 >> inst->op2) |
                         (inst->op1 << (32 - inst->op2)));
                    break;
                case IR_ADD:
                    if (inst[1].opcode == IR_GETC ||
                        inst[3].opcode == IR_GETV) {
                        ADDCV(inst->op1, inst->op2, 0);
                    } else {
                        OPTI(inst->op1 + inst->op2);
                        if (inst[1].opcode == IR_ADC) {
                            *inst = (IRInstr){.opcode = IR_SETC,
                                              .imm1 = 1,
                                              .imm2 = 1,
                                              .op1 = 0,
                                              .op2 = inst->op1 + inst->op2 <
                                                     inst->op1};
                        }
                    }
                    break;
                case IR_SUB:
                    if (inst[1].opcode == IR_GETC) {
                        ADDCV(inst->op1, ~inst->op2, 1);
                    } else {
                        OPTI(inst->op1 - inst->op2);
                    }
                    break;
                case IR_GETN:
                    OPTI(inst->op2 >> 31);
                    break;
                case IR_GETZ:
                    OPTI(inst->op2 == 0);
                    break;
                case IR_GETCIFZ:
                    if (inst->op1) {
                        OPTI(inst->op2);
                    } else {
                        if (inst[-1].opcode == IR_SETC) {
                            if (inst[-1].imm2) {
                                OPTI(inst[-1].op2);
                            } else {
                                OPTV(inst[-1].op2);
                            }
                            inst[-1] = NOP;
                        } else {
                            NOOPT();
                        }
                    }
                    break;
                case IR_PCMASK:
                    OPTI(inst->op1 ? ~1 : ~3);
                    break;
                case IR_JZ:
                    constant_jmp_helper(block, inst, inst->op1 == 0);
                    break;
                case IR_JNZ:
                    constant_jmp_helper(block, inst, inst->op1 != 0);
                    break;
                default:
                    NOOPT();
                    break;
            }
        } else if (inst->imm2) {
            switch (inst->opcode) {
                case IR_AND:
                    if (inst->op2 == 0) {
                        OPTI(0);
                    } else if (inst->op2 == 0xffffffff) {
                        OPTV(inst->op1);
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_OR:
                    if (inst->op2 == 0xffffffff) {
                        OPTI(0xffffffff);
                    } else if (inst->op2 == 0) {
                        OPTV(inst->op1);
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_XOR:
                    if (inst->op2 == 0) {
                        OPTV(inst->op1);
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_LSL:
                    if (inst->op2 >= 32) {
                        OPTI(0);
                    } else if (inst->op2 == 0) {
                        OPTV(inst->op1);
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_LSR:
                    if (inst->op2 >= 32) {
                        OPTI(0);
                    } else if (inst->op2 == 0) {
                        OPTV(inst->op1);
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_ASR:
                    if (inst->op2 == 0) {
                        OPTV(inst->op1);
                    } else {
                        if (inst->op2 >= 32) {
                            inst->op2 = 31;
                        }
                        NOOPT();
                    }
                    break;
                case IR_ROR:
                    if (inst->op2 == 0) {
                        OPTV(inst->op1);
                    } else {
                        inst->op2 &= 31;
                        NOOPT();
                    }
                    break;
                case IR_ADD:
                    if (inst->op2 == 0) {
                        OPTV(inst->op1);
                        if (inst[1].opcode == IR_GETC) inst[1] = MOVI(0);
                        if (inst[3].opcode == IR_GETV) inst[3] = MOVI(0);
                        if (inst[1].opcode == IR_ADC) {
                            *inst = (IRInstr){.opcode = IR_SETC,
                                              .imm1 = 1,
                                              .imm2 = 1,
                                              .op1 = 0,
                                              .op2 = 0};
                        }
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_SUB:
                    if (inst->op2 == 0) {
                        OPTV(inst->op1);
                        if (inst[1].opcode == IR_GETC) inst[1] = MOVI(1);
                        if (inst[3].opcode == IR_GETV) inst[3] = MOVI(0);
                    } else {
                        NOOPT();
                    }
                    break;
                default:
                    NOOPT();
                    break;
            }
        } else if (inst->imm1) {
            switch (inst->opcode) {
                case IR_MOV:
                    OPTV(inst->op2);
                    break;
                case IR_AND:
                    if (inst->op1 == 0) {
                        OPTI(0);
                    } else if (inst->op1 == 0xffffffff) {
                        OPTV(inst->op2);
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_OR:
                    if (inst->op1 == 0xffffffff) {
                        OPTI(0xffffffff);
                    } else if (inst->op1 == 0) {
                        OPTV(inst->op2);
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_XOR:
                    if (inst->op1 == 0) {
                        OPTV(inst->op2);
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_LSL:
                case IR_LSR:
                case IR_ASR:
                case IR_ROR:
                    if (inst->op1 == 0) {
                        OPTI(0);
                    } else if (inst->op1 == 0xffffffff &&
                               (inst->opcode == IR_ASR ||
                                inst->opcode == IR_ROR)) {
                        OPTI(0xffffffff);
                    } else {
                        NOOPT();
                    }
                    break;
                case IR_ADD:
                    if (inst->op1 == 0) {
                        OPTV(inst->op2);
                        if (inst[1].opcode == IR_GETC) inst[1] = MOVI(0);
                        if (inst[3].opcode == IR_GETV) inst[3] = MOVI(0);
                        if (inst[1].opcode == IR_ADC) {
                            *inst = (IRInstr){.opcode = IR_SETC,
                                              .imm1 = 1,
                                              .imm2 = 1,
                                              .op1 = 0,
                                              .op2 = 0};
                        }
                    } else {
                        NOOPT();
                    }
                    break;
                default:
                    NOOPT();
                    break;
            }
        } else if (inst->op1 == inst->op2) {
            switch (inst->opcode) {
                case IR_AND:
                case IR_OR:
                    OPTV(inst->op1);
                    break;
                case IR_XOR:
                    OPTI(0);
                    break;
                case IR_SUB:
                    OPTI(0);
                    if (inst[1].opcode == IR_GETC) inst[1] = MOVI(1);
                    if (inst[3].opcode == IR_GETV) inst[3] = MOVI(0);
                    break;
                default:
                    NOOPT();
                    break;
            }
        } else {
            NOOPT();
        }

        if (i == jmptarget) {
            for (int j = jmpsrc; j < jmptarget; j++) {
                notcurcond[j] = true;
            }
        }
        if (iropc_ispure(inst->opcode) &&
            !(inst[1].opcode == IR_ADC || inst[1].opcode == IR_GETC ||
              (inst + 3 < block->code.d + block->code.size &&
               inst[3].opcode == IR_GETV))) {
            for (int j = 0; j < i; j++) {
                if (notcurcond[j]) continue;
                if (!memcmp(&block->code.d[j], inst, sizeof(IRInstr))) {
                    *inst = MOVX(j, 0);
                    break;
                }
            }
        }
        notcurcond[i] = false;
        if (inst->opcode == IR_JZ || inst->opcode == IR_JNZ ||
            inst->opcode == IR_JELSE) {
            jmpsrc = i;
            jmptarget = inst->op2;
        }
    }
}

void optimize_literals(IRBlock* block, ArmCore* cpu) {
    u32 latest_const = block->end_addr + BIT(10);
    if (latest_const > block->start_addr + MAX_BLOCK_SIZE)
        latest_const = block->start_addr + MAX_BLOCK_SIZE;
    for (int i = 0; i < block->code.size; i++) {
        IRInstr* inst = &block->code.d[i];
        switch (inst->opcode) {
            case IR_LOAD_MEM8:
            case IR_LOAD_MEMS8:
            case IR_LOAD_MEM16:
            case IR_LOAD_MEMS16:
            case IR_LOAD_MEM32:
                if (inst->op1 >= block->start_addr &&
                    inst->op1 < latest_const) {
                    switch (inst->opcode) {
                        case IR_LOAD_MEM8:
                            *inst = MOVI(cpu->read8(cpu, inst->op1, false));
                            break;
                        case IR_LOAD_MEMS8:
                            *inst = MOVI(cpu->read8(cpu, inst->op1, true));
                            break;
                        case IR_LOAD_MEM16:
                            *inst = MOVI(cpu->read16(cpu, inst->op1, false));
                            break;
                        case IR_LOAD_MEMS16:
                            *inst = MOVI(cpu->read16(cpu, inst->op1, true));
                            break;
                        case IR_LOAD_MEM32:
                            *inst = MOVI(cpu->read32(cpu, inst->op1));
                            break;
                        default:
                            break;
                    }
                    if (inst->op1 > block->end_addr)
                        block->end_addr = inst->op1;
                }
                break;
            default:
                break;
        }
    }
}

u32 chainjmp_helper(IRBlock* block, IRInstr* jmp) {
    IRInstr* next = &block->code.d[jmp->op2];
    while (next->opcode == IR_NOP) next++;
    if (next->op1 == jmp->op1 &&
        (next->opcode == IR_JZ || next->opcode == IR_JNZ)) {
        chainjmp_helper(block, next);
        if (next->opcode == jmp->opcode) {
            jmp->op2 = next->op2;
            *next = NOP;
        } else {
            *next = (IRInstr){.opcode = IR_JELSE,
                              .imm1 = 1,
                              .imm2 = 1,
                              .op1 = 0,
                              .op2 = next->op2};
        }
    }
    return jmp->op2;
}

void optimize_chainjumps(IRBlock* block) {
    for (int i = 0; i < block->code.size; i++) {
        IRInstr inst = block->code.d[i];
        switch (inst.opcode) {
            case IR_JZ:
            case IR_JNZ:
                i = chainjmp_helper(block, &block->code.d[i]) - 1;
                break;
            case IR_JELSE:
                i = inst.op2 - 1;
                break;
            case IR_END_RET:
            case IR_END_LINK:
                for (int j = i + 1; j < block->code.size; j++) {
                    block->code.d[j] = NOP;
                }
                return;
            default:
                break;
        }
    }
}

void optimize_deadcode(IRBlock* block) {
    bool vused[block->code.size];
    for (int i = 0; i < block->code.size; i++) vused[i] = false;
    for (int i = block->code.size - 1; i >= 0; i--) {
        IRInstr* inst = &block->code.d[i];
        if (!vused[i] && iropc_hasresult(inst->opcode) &&
            !iropc_iscallback(inst->opcode)) {
            if (!(inst->opcode == IR_ADD && inst[1].opcode == IR_ADC))
                *inst = NOP;
        }
        if (!inst->imm1) vused[inst->op1] = true;
        if (!inst->imm2) vused[inst->op2] = true;
    }
}

void optimize_waitloop(IRBlock* block) {
#define R(n) BIT(n)
#define CPSR BIT(16)
#define SPSR BIT(17)
#define MEM BIT(18)
#define LOAD(v) (loaded |= v);
#define STORE(v) (loaded & (v) ? modified |= v : 0)

    u32 loaded = 0;
    u32 modified = 0;

    for (int i = 0; i < block->code.size; i++) {
        IRInstr inst = block->code.d[i];
        switch (inst.opcode) {
            case IR_LOAD_REG:
                LOAD(R(inst.op1));
                break;
            case IR_STORE_REG:
                if (inst.op1 == 15 && inst.imm2 &&
                    inst.op2 == block->start_addr &&
                    block->code.d[i + 1].opcode == IR_NOP && !modified) {
                    block->code.d[i + 1].opcode = IR_WFE;
                    return;
                } else {
                    STORE(R(inst.op1));
                }
                break;
            case IR_LOAD_FLAG:
                LOAD(CPSR);
                break;
            case IR_STORE_FLAG:
                STORE(CPSR);
                break;
            case IR_LOAD_CPSR:
                LOAD(CPSR);
                break;
            case IR_STORE_CPSR:
                STORE(CPSR);
                break;
            case IR_LOAD_SPSR:
                LOAD(SPSR);
                break;
            case IR_STORE_SPSR:
                STORE(SPSR);
                break;
            case IR_LOAD_MEM8:
                LOAD(MEM);
                break;
            case IR_LOAD_MEMS8:
                LOAD(MEM);
                break;
            case IR_LOAD_MEM16:
                LOAD(MEM);
                break;
            case IR_LOAD_MEMS16:
                LOAD(MEM);
                break;
            case IR_LOAD_MEM32:
                LOAD(MEM);
                break;
            case IR_STORE_MEM8:
                STORE(MEM);
                break;
            case IR_STORE_MEM16:
                STORE(MEM);
                break;
            case IR_STORE_MEM32:
                STORE(MEM);
                break;
            case IR_VFP_DATA_PROC:
            case IR_VFP_LOAD_MEM:
            case IR_VFP_STORE_MEM:
            case IR_VFP_READ:
            case IR_VFP_WRITE:
            case IR_CP15_READ:
            case IR_CP15_WRITE:
                return;
            default:
                break;
        }
    }

#undef R
#undef CPSR
#undef SPSR
#undef MEM
#undef LOAD
#undef STORE
}

void optimize_blocklinking(IRBlock* block, ArmCore* cpu) {
    bool can_link = true;
    bool link_thumb = cpu->cpsr.t;
    u32 link_pc = 0;
    for (int i = 0; i < block->code.size; i++) {
        IRInstr* inst = &block->code.d[i];
        switch (inst->opcode) {
            case IR_STORE_REG:
                if (inst->op1 == 15) {
                    if (inst->imm2) {
                        link_pc = inst->op2;
                    } else {
                        can_link = false;
                    }
                }
                break;
            case IR_STORE_THUMB:
                if (inst->imm2) {
                    link_thumb = inst->op2;
                } else {
                    can_link = false;
                }
                break;
            case IR_MODESWITCH:
            case IR_EXCEPTION:
            case IR_WFE:
            case IR_CP15_WRITE:
                can_link = false;
                break;
            case IR_END_RET:
                if (can_link) {
                    if (link_pc == block->start_addr &&
                        link_thumb == cpu->cpsr.t) {
                        inst->opcode = IR_END_LOOP;
                    } else {
                        inst->opcode = IR_END_LINK;
                        inst->op1 = cpu->cpsr.m | (link_thumb << 5);
                        inst->op2 = link_pc;
                    }
                }
                can_link = true;
                break;
            default:
                break;
        }
    }
}
