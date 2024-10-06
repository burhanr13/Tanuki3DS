#include "ir.h"

#include "../media.h"
#include "../vfp.h"
#include "jit.h"

bool iropc_hasresult(IROpcode opc) {
    if (opc > IR_NOP) return true;
    switch (opc) {
        case IR_LOAD_REG:
        case IR_LOAD_FLAG:
        case IR_LOAD_REG_USR:
        case IR_LOAD_CPSR:
        case IR_LOAD_SPSR:
        case IR_LOAD_THUMB:
        case IR_LOAD_MEM8:
        case IR_LOAD_MEMS8:
        case IR_LOAD_MEM16:
        case IR_LOAD_MEMS16:
        case IR_LOAD_MEM32:
        case IR_VFP_READ:
        case IR_VFP_READ64L:
        case IR_VFP_READ64H:
        case IR_CP15_READ:
            return true;
        default:
            return false;
    }
}

bool iropc_iscallback(IROpcode opc) {
    switch (opc) {
        case IR_LOAD_MEM8:
        case IR_LOAD_MEMS8:
        case IR_LOAD_MEM16:
        case IR_LOAD_MEMS16:
        case IR_LOAD_MEM32:
        case IR_STORE_MEM8:
        case IR_STORE_MEM16:
        case IR_STORE_MEM32:
        case IR_MODESWITCH:
        case IR_EXCEPTION:
        case IR_VFP_DATA_PROC:
        case IR_VFP_LOAD_MEM:
        case IR_VFP_STORE_MEM:
        case IR_VFP_READ:
        case IR_VFP_WRITE:
        case IR_VFP_READ64L:
        case IR_VFP_READ64H:
        case IR_VFP_WRITE64L:
        case IR_VFP_WRITE64H:
        case IR_CP15_READ:
        case IR_CP15_WRITE:
        case IR_MEDIA_UADD8:
        case IR_MEDIA_UQSUB8:
        case IR_MEDIA_SEL:
            return true;
        default:
            return false;
    }
}

bool iropc_ispure(IROpcode opc) {
    if (opc <= IR_NOP) return false;
    switch (opc) {
        case IR_RRC:
        case IR_ADC:
        case IR_SBC:
        case IR_GETCIFZ:
        case IR_MEDIA_UADD8:
        case IR_MEDIA_UQSUB8:
        case IR_MEDIA_SEL:
            return false;
        default:
            return true;
    }
}

#define OP(n)                                                                  \
    (block->code.d[i].imm##n ? block->code.d[i].op##n                          \
                             : v[block->code.d[i].op##n])

#define ADDCV(op1, op2, c)                                                     \
    do {                                                                       \
        v[i] = op1 + op2;                                                      \
        u32 tmpc = v[i] < op1;                                                 \
        v[i] += c;                                                             \
        cf = tmpc || v[i] < op1 + op2;                                         \
        vf = ((op1 ^ v[i]) & ~(op1 ^ op2)) >> 31;                              \
    } while (false)

void ir_interpret(IRBlock* block, ArmCore* cpu) {
    u32 i = 0;
    u32 v[block->code.size];

#ifdef IR_TRACE
    if (block->start_addr == IR_TRACE_ADDR) {
        printf("======== tracing IR block 0x%08x =========\n",
               block->start_addr);
    }
#endif

    bool cf = 0, vf = 0;
    bool jmptaken;
    while (true) {
        while (block->code.d[i].opcode == IR_NOP) i++;
        switch (block->code.d[i].opcode) {
            case IR_LOAD_REG:
                v[i] = cpu->r[OP(1)];
                break;
            case IR_STORE_REG:
                cpu->r[OP(1)] = OP(2);
                break;
            case IR_LOAD_FLAG:
                v[i] = (cpu->cpsr.w & BIT(31 - OP(1))) ? 1 : 0;
                break;
            case IR_STORE_FLAG:
                cpu->cpsr.w &= ~BIT(31 - OP(1));
                if (OP(2)) cpu->cpsr.w |= BIT(31 - OP(1));
                break;
            case IR_LOAD_REG_USR: {
                int rd = OP(1);
                if (rd < 13) {
                    v[i] = cpu->banked_r8_12[0][rd - 8];
                } else if (rd == 13) {
                    v[i] = cpu->banked_sp[0];
                } else if (rd == 14) {
                    v[i] = cpu->banked_lr[0];
                }
                break;
            }
            case IR_STORE_REG_USR: {
                int rd = OP(1);
                if (rd < 13) {
                    cpu->banked_r8_12[0][rd - 8] = OP(2);
                } else if (rd == 13) {
                    cpu->banked_sp[0] = OP(2);
                } else if (rd == 14) {
                    cpu->banked_lr[0] = OP(2);
                }
                break;
            }
            case IR_LOAD_CPSR:
                v[i] = cpu->cpsr.w;
                break;
            case IR_STORE_CPSR:
                cpu->cpsr.w = OP(2);
                break;
            case IR_LOAD_SPSR:
                v[i] = cpu->spsr;
                break;
            case IR_STORE_SPSR:
                cpu->spsr = OP(2);
                break;
            case IR_LOAD_THUMB:
                v[i] = cpu->cpsr.t;
                break;
            case IR_STORE_THUMB:
                cpu->cpsr.t = OP(2);
                break;
            case IR_VFP_DATA_PROC: {
                ArmInstr vfpinst = {OP(1)};
                exec_vfp_data_proc(cpu, vfpinst);
                break;
            }
            case IR_VFP_LOAD_MEM: {
                ArmInstr vfpinst = {OP(1)};
                exec_vfp_load_mem(cpu, vfpinst, OP(2));
                break;
            }
            case IR_VFP_STORE_MEM: {
                ArmInstr vfpinst = {OP(1)};
                exec_vfp_store_mem(cpu, vfpinst, OP(2));
                break;
            }
            case IR_VFP_READ: {
                ArmInstr vfpinst = {OP(1)};
                v[i] = exec_vfp_read(cpu, vfpinst);
                break;
            }
            case IR_VFP_WRITE: {
                ArmInstr vfpinst = {OP(1)};
                exec_vfp_write(cpu, vfpinst, OP(2));
                break;
            }
            case IR_VFP_READ64L: {
                ArmInstr vfpinst = {OP(1)};
                u64 d = exec_vfp_read64(cpu, vfpinst);
                v[i] = d;
                i++;
                v[i] = d >> 32;
                break;
            }
            case IR_VFP_READ64H:
                break;
            case IR_VFP_WRITE64L: {
                ArmInstr vfpinst = {OP(1)};
                u64 d = OP(2);
                i++;
                d |= (u64) OP(2) << 32;
                exec_vfp_write64(cpu, vfpinst, d);
                break;
            }
            case IR_VFP_WRITE64H:
                break;
            case IR_CP15_READ: {
                ArmInstr cpinst = {OP(1)};
                v[i] = cpu->cp15_read(cpu, cpinst.cp_reg_trans.crn,
                                      cpinst.cp_reg_trans.crm,
                                      cpinst.cp_reg_trans.cp);
                break;
            }
            case IR_CP15_WRITE: {
                ArmInstr cpinst = {OP(1)};
                cpu->cp15_write(cpu, cpinst.cp_reg_trans.crn,
                                cpinst.cp_reg_trans.crm, cpinst.cp_reg_trans.cp,
                                OP(2));
                break;
            }
            case IR_LOAD_MEM8:
                v[i] = cpu->read8(cpu, OP(1), false);
                break;
            case IR_LOAD_MEMS8:
                v[i] = cpu->read8(cpu, OP(1), true);
                break;
            case IR_LOAD_MEM16:
                v[i] = cpu->read16(cpu, OP(1), false);
                break;
            case IR_LOAD_MEMS16:
                v[i] = cpu->read16(cpu, OP(1), true);
                break;
            case IR_LOAD_MEM32:
                v[i] = cpu->read32(cpu, OP(1));
                break;
            case IR_STORE_MEM8:
                cpu->write8(cpu, OP(1), OP(2));
                break;
            case IR_STORE_MEM16:
                cpu->write16(cpu, OP(1), OP(2));
                break;
            case IR_STORE_MEM32:
                cpu->write32(cpu, OP(1), OP(2));
                break;
            case IR_NOP:
                break;
            case IR_MOV:
                v[i] = OP(2);
                break;
            case IR_AND:
                v[i] = OP(1) & OP(2);
                break;
            case IR_OR:
                v[i] = OP(1) | OP(2);
                break;
            case IR_XOR:
                v[i] = OP(1) ^ OP(2);
                break;
            case IR_NOT:
                v[i] = ~OP(2);
                break;
            case IR_LSL: {
                u32 shamt = OP(2);
                if (shamt >= 32) {
                    v[i] = 0;
                } else {
                    v[i] = OP(1) << shamt;
                }
                break;
            }
            case IR_LSR: {
                u32 shamt = OP(2);
                if (shamt >= 32) {
                    v[i] = 0;
                } else {
                    v[i] = OP(1) >> shamt;
                }
                break;
            }
            case IR_ASR: {
                u32 shamt = OP(2);
                if (shamt >= 32) {
                    shamt = 31;
                }
                v[i] = (s32) OP(1) >> shamt;
                break;
            }
            case IR_ROR: {
                u32 shamt = OP(2);
                if (shamt >= 32) {
                    shamt &= 0x1f;
                }
                v[i] = OP(1) >> shamt | OP(1) << (32 - shamt);
                break;
            }
            case IR_RRC:
                v[i] = OP(1) >> 1 | cf << 31;
                break;
            case IR_ADD:
                if (block->code.d[i + 1].opcode == IR_GETC ||
                    block->code.d[i + 3].opcode == IR_GETV ||
                    block->code.d[i + 1].opcode == IR_ADC) {
                    ADDCV(OP(1), OP(2), 0);
                } else {
                    v[i] = OP(1) + OP(2);
                }
                break;
            case IR_SUB:
                if (block->code.d[i + 1].opcode == IR_GETC ||
                    block->code.d[i + 3].opcode == IR_GETV) {
                    ADDCV(OP(1), ~OP(2), 1);
                } else {
                    v[i] = OP(1) - OP(2);
                }
                break;
            case IR_ADC:
                if (block->code.d[i + 1].opcode == IR_GETC ||
                    block->code.d[i + 3].opcode == IR_GETV) {
                    ADDCV(OP(1), OP(2), cf);
                } else {
                    v[i] = OP(1) + OP(2) + cf;
                }
                break;
            case IR_SBC:
                if (block->code.d[i + 1].opcode == IR_GETC ||
                    block->code.d[i + 3].opcode == IR_GETV) {
                    ADDCV(OP(1), ~OP(2), cf);
                } else {
                    v[i] = OP(1) + ~OP(2) + cf;
                }
                break;
            case IR_MUL:
                v[i] = OP(1) * OP(2);
                break;
            case IR_UMULH:
                v[i] = ((u64) OP(1) * (u64) OP(2)) >> 32;
                break;
            case IR_SMULH:
                v[i] = ((s64) (s32) OP(1) * (s64) (s32) OP(2)) >> 32;
                break;
            case IR_SMULW:
                v[i] = ((s64) (s32) OP(1) * (s64) (s32) OP(2)) >> 16;
                break;
            case IR_CLZ: {
                u32 op = OP(2);
                u32 ct = 0;
                if (op == 0) ct = 32;
                else
                    while (!(op & BIT(31))) {
                        op <<= 1;
                        ct++;
                    }
                v[i] = ct;
                break;
            }
            case IR_REV: {
                u32 x = OP(2);
                x = (x & 0xffff0000) >> 16 | (x & 0x0000ffff) << 16;
                x = (x & 0xff00ff00) >> 8 | (x & 0x00ff00ff) << 8;
                v[i] = x;
                break;
            }
            case IR_REV16: {
                u32 x = OP(2);
                x = (x & 0xff00ff00) >> 8 | (x & 0x00ff00ff) << 8;
                v[i] = x;
                break;
            }
            case IR_USAT: {
                s32 x = OP(2);
                if (x < 0) x = 0;
                if (x > MASK(OP(1))) x = MASK(OP(1));
                v[i] = x;
                break;
            }
            case IR_MEDIA_UADD8: {
                v[i] = media_uadd8(cpu, OP(1), OP(2));
                break;
            }
            case IR_MEDIA_UQSUB8: {
                v[i] = media_uqsub8(cpu, OP(1), OP(2));
                break;
            }
            case IR_MEDIA_SEL: {
                v[i] = media_sel(cpu, OP(1), OP(2));
                break;
            }
            case IR_GETN:
                v[i] = OP(2) >> 31;
                break;
            case IR_GETZ:
                v[i] = OP(2) == 0;
                break;
            case IR_GETC:
                v[i] = cf;
                break;
            case IR_GETV:
                v[i] = vf;
                break;
            case IR_SETC:
                cf = OP(2);
                break;
            case IR_GETCIFZ:
                v[i] = OP(1) ? OP(2) : cf;
                break;
            case IR_PCMASK:
                v[i] = OP(1) ? ~1 : ~3;
                break;
            case IR_JZ:
                if (OP(1) == 0) {
                    i = OP(2) - 1;
                    jmptaken = true;
                } else jmptaken = false;
                break;
            case IR_JNZ:
                if (OP(1) != 0) {
                    i = OP(2) - 1;
                    jmptaken = true;
                } else jmptaken = false;
                break;
            case IR_JELSE:
                if (!jmptaken) {
                    i = OP(2) - 1;
                    jmptaken = true;
                } else jmptaken = false;
                break;
            case IR_MODESWITCH:
                cpu_update_mode(cpu, OP(1));
                break;
            case IR_EXCEPTION:
                switch (OP(1)) {
                    case E_SWI:
                        cpu->handle_svc(cpu, (ArmInstr){OP(2)}.sw_intr.arg);
                        break;
                    case E_UND:
                        cpu_undefined_fail(cpu, OP(2));
                        break;
                }
                break;
            case IR_WFE:
                cpu->wfe = true;
                break;
            case IR_BEGIN:
                break;
            case IR_END_LINK:
                cpu->cycles -= block->numinstr;
                if (cpu->cycles > 0) {
                    JITBlock* next = get_jitblock(cpu, OP(1), OP(2));
                    if (next) {
                        jit_exec(next);
                        return;
                    }
                }
                return;
            case IR_END_RET:
                cpu->cycles -= block->numinstr;
                return;
        }
#ifdef IR_TRACE
        if (block->start_addr == IR_TRACE_ADDR) {
            ir_disasm_instr(inst, i);
            printf(" (v%d = 0x%x, cf=%d)\n", i, v[i], cf);
        }
#endif
        i++;
    }
}

#define DISASM_OP(n)                                                           \
    ((inst.imm##n) ? printf("0x%x", inst.op##n) : printf("v%d", inst.op##n))

#define DISASM(name, r, op1, op2)                                              \
    if (r) printf("v%d = ", i);                                                \
    printf(#name);                                                             \
    if (op1) {                                                                 \
        printf(" ");                                                           \
        DISASM_OP(1);                                                          \
    }                                                                          \
    if (op2) {                                                                 \
        printf(" ");                                                           \
        DISASM_OP(2);                                                          \
    }                                                                          \
    return

#define DISASM_REG(name, r, op2)                                               \
    if (r) printf("v%d = ", i);                                                \
    printf(#name);                                                             \
    printf(" r%d ", inst.op1);                                                 \
    if (op2) DISASM_OP(2);                                                     \
    return

#define DISASM_FLAG(name, r, op2)                                              \
    if (r) printf("v%d = ", i);                                                \
    printf(#name);                                                             \
    printf(" %s ", inst.op1 == NF   ? "n"                                      \
                   : inst.op1 == ZF ? "z"                                      \
                   : inst.op1 == CF ? "c"                                      \
                   : inst.op1 == VF ? "v"                                      \
                                    : "q");                                    \
    if (op2) DISASM_OP(2);                                                     \
    return

#define DISASM_MEM(name, r, op2)                                               \
    if (r) printf("v%d = ", i);                                                \
    printf(#name " ");                                                         \
    printf("[");                                                               \
    DISASM_OP(1);                                                              \
    printf("] ");                                                              \
    if (op2) DISASM_OP(2);                                                     \
    return

#define DISASM_JMP(name, op1)                                                  \
    printf(#name);                                                             \
    if (op1) {                                                                 \
        printf(" ");                                                           \
        DISASM_OP(1);                                                          \
    }                                                                          \
    printf(" %d", inst.op2);                                                   \
    return

void ir_disasm_instr(IRInstr inst, int i) {
    switch (inst.opcode) {
        case IR_LOAD_REG:
            DISASM_REG(load_reg, 1, 0);
        case IR_STORE_REG:
            DISASM_REG(store_reg, 0, 1);
        case IR_LOAD_FLAG:
            DISASM_FLAG(load_flag, 1, 0);
        case IR_STORE_FLAG:
            DISASM_FLAG(store_flag, 0, 1);
        case IR_LOAD_REG_USR:
            DISASM_REG(load_reg_usr, 1, 0);
        case IR_STORE_REG_USR:
            DISASM_REG(store_reg_usr, 0, 1);
        case IR_LOAD_CPSR:
            DISASM(load_cpsr, 1, 0, 0);
        case IR_STORE_CPSR:
            DISASM(store_cpsr, 0, 0, 1);
        case IR_LOAD_SPSR:
            DISASM(load_spsr, 1, 0, 0);
        case IR_STORE_SPSR:
            DISASM(store_spsr, 0, 0, 1);
        case IR_LOAD_THUMB:
            DISASM(load_thumb, 1, 0, 0);
        case IR_STORE_THUMB:
            DISASM(store_thumb, 0, 0, 1);
        case IR_LOAD_MEM8:
            DISASM_MEM(load_mem8, 1, 0);
        case IR_LOAD_MEMS8:
            DISASM_MEM(load_mems8, 1, 0);
        case IR_LOAD_MEM16:
            DISASM_MEM(load_mem16, 1, 0);
        case IR_LOAD_MEMS16:
            DISASM_MEM(load_mems16, 1, 0);
        case IR_LOAD_MEM32:
            DISASM_MEM(load_mem32, 1, 0);
        case IR_STORE_MEM8:
            DISASM_MEM(store_mem8, 0, 1);
        case IR_STORE_MEM16:
            DISASM_MEM(store_mem16, 0, 1);
        case IR_STORE_MEM32:
            DISASM_MEM(store_mem32, 0, 1);
        case IR_VFP_DATA_PROC:
            DISASM(vfp_data_proc, 0, 1, 0);
        case IR_VFP_LOAD_MEM:
            DISASM(vfp_load_mem, 0, 1, 1);
        case IR_VFP_STORE_MEM:
            DISASM(vfp_store_mem, 0, 1, 1);
        case IR_VFP_READ:
            DISASM(vfp_read, 1, 1, 0);
        case IR_VFP_WRITE:
            DISASM(vfp_write, 0, 1, 1);
        case IR_VFP_READ64L:
            DISASM(vfp_read64l, 1, 1, 0);
        case IR_VFP_READ64H:
            DISASM(vfp_read64h, 1, 1, 0);
        case IR_VFP_WRITE64L:
            DISASM(vfp_write64l, 0, 1, 1);
        case IR_VFP_WRITE64H:
            DISASM(vfp_write64h, 0, 1, 1);
        case IR_CP15_READ:
            DISASM(cp15_read, 1, 1, 0);
        case IR_CP15_WRITE:
            DISASM(cp15_write, 0, 1, 1);
        case IR_NOP:
            return;
        case IR_MOV:
            DISASM(mov, 1, 0, 1);
        case IR_AND:
            DISASM(and, 1, 1, 1);
        case IR_OR:
            DISASM(or, 1, 1, 1);
        case IR_XOR:
            DISASM(xor, 1, 1, 1);
        case IR_NOT:
            DISASM(not, 1, 0, 1);
        case IR_LSL:
            DISASM(lsl, 1, 1, 1);
        case IR_LSR:
            DISASM(lsr, 1, 1, 1);
        case IR_ASR:
            DISASM(asr, 1, 1, 1);
        case IR_ROR:
            DISASM(ror, 1, 1, 1);
        case IR_RRC:
            DISASM(rrc, 1, 1, 0);
        case IR_ADD:
            DISASM(add, 1, 1, 1);
        case IR_SUB:
            DISASM(sub, 1, 1, 1);
        case IR_ADC:
            DISASM(adc, 1, 1, 1);
        case IR_SBC:
            DISASM(sbc, 1, 1, 1);
        case IR_MUL:
            DISASM(mul, 1, 1, 1);
        case IR_UMULH:
            DISASM(umulh, 1, 1, 1);
        case IR_SMULH:
            DISASM(smulh, 1, 1, 1);
        case IR_SMULW:
            DISASM(smulw, 1, 1, 1);
        case IR_CLZ:
            DISASM(clz, 1, 0, 1);
        case IR_REV:
            DISASM(rev, 1, 0, 1);
        case IR_REV16:
            DISASM(rev, 1, 0, 1);
        case IR_USAT:
            DISASM(usat, 1, 1, 1);
        case IR_MEDIA_UADD8:
            DISASM(uadd8, 1, 1, 1);
        case IR_MEDIA_UQSUB8:
            DISASM(uqsub8, 1, 1, 1);
        case IR_MEDIA_SEL:
            DISASM(sel, 1, 1, 1);
        case IR_GETN:
            DISASM(getn, 1, 0, 1);
        case IR_GETZ:
            DISASM(getz, 1, 0, 1);
        case IR_GETC:
            DISASM(getc, 1, 0, 1);
        case IR_GETV:
            DISASM(getv, 1, 0, 1);
        case IR_SETC:
            DISASM(setc, 0, 0, 1);
        case IR_GETCIFZ:
            DISASM(getcifz, 1, 1, 1);
        case IR_PCMASK:
            DISASM(pcmask, 1, 1, 0);
        case IR_JZ:
            DISASM_JMP(jz, 1);
        case IR_JNZ:
            DISASM_JMP(jnz, 1);
        case IR_JELSE:
            DISASM_JMP(jelse, 0);
        case IR_MODESWITCH:
            DISASM(modeswitch, 0, 1, 0);
            break;
        case IR_EXCEPTION:
            DISASM(exception, 0, 1, 1);
            break;
        case IR_WFE:
            DISASM(wfe, 0, 0, 0);
            break;
        case IR_BEGIN:
            DISASM(begin, 0, 0, 0);
        case IR_END_RET:
            DISASM(end_ret, 0, 0, 0);
        case IR_END_LINK:
            DISASM(end_link, 0, 1, 1);
    }
}

void ir_disassemble(IRBlock* block) {
    printf("============= IR Block 0x%08x =================\n",
           block->start_addr);
    u32 jmptarget = -1;
    for (int i = 0; i < block->code.size; i++) {
        if (i == jmptarget) printf("%d:\n", i);
        if (block->code.d[i].opcode == IR_NOP) continue;
        if (block->code.d[i].opcode == IR_JZ ||
            block->code.d[i].opcode == IR_JNZ ||
            block->code.d[i].opcode == IR_JELSE)
            jmptarget = block->code.d[i].op2;
        ir_disasm_instr(block->code.d[i], i);
        printf("\n");
    }
}
