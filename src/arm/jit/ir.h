#ifndef IR_H
#define IR_H

#include <stdlib.h>

#include "../arm_core.h"
#include "../../types.h"

enum { NF, ZF, CF, VF, QF };

typedef enum {
    IR_LOAD_REG,
    IR_STORE_REG,
    IR_LOAD_FLAG,
    IR_STORE_FLAG,
    IR_LOAD_REG_USR,
    IR_STORE_REG_USR,
    IR_LOAD_CPSR,
    IR_STORE_CPSR,
    IR_LOAD_SPSR,
    IR_STORE_SPSR,
    IR_LOAD_THUMB,
    IR_STORE_THUMB,

    IR_LOAD_MEM8,
    IR_LOAD_MEMS8,
    IR_LOAD_MEM16,
    IR_LOAD_MEMS16,
    IR_LOAD_MEM32,
    IR_STORE_MEM8,
    IR_STORE_MEM16,
    IR_STORE_MEM32,

    IR_VFP_DATA_PROC,
    IR_VFP_LOAD_MEM,
    IR_VFP_STORE_MEM,
    IR_VFP_READ,
    IR_VFP_WRITE,
    IR_VFP_READ64L,
    IR_VFP_READ64H,
    IR_VFP_WRITE64L,
    IR_VFP_WRITE64H,

    IR_CP15_READ,
    IR_CP15_WRITE,

    IR_SETC,

    IR_JZ,
    IR_JNZ,
    IR_JELSE,

    IR_MODESWITCH,
    IR_EXCEPTION,
    IR_WFE,

    IR_BEGIN,
    IR_END_RET,
    IR_END_LINK,

    IR_NOP,
    IR_MOV,

    IR_AND,
    IR_OR,
    IR_XOR,
    IR_NOT,

    IR_LSL,
    IR_LSR,
    IR_ASR,
    IR_ROR,
    IR_RRC,

    IR_ADD,
    IR_SUB,
    IR_ADC,
    IR_SBC,

    IR_MUL,
    IR_SMULH,
    IR_UMULH,
    IR_SMULW,
    IR_CLZ,
    IR_REV,
    IR_REV16,
    IR_USAT,

    IR_MEDIA_UADD8,
    IR_MEDIA_UQSUB8,
    IR_MEDIA_SEL,

    IR_GETN,
    IR_GETZ,
    IR_GETC,
    IR_GETV,

    IR_GETCIFZ,

    IR_PCMASK,

} IROpcode;

typedef struct {
    struct {
        IROpcode opcode : 30;
        u32 imm1 : 1;
        u32 imm2 : 1;
    };
    u32 op1;
    u32 op2;
} IRInstr;

typedef struct {
    Vector(IRInstr) code;
    u32 start_addr;
    u32 end_addr;
    u32 numinstr;
    bool loop;
} IRBlock;

static inline void irblock_init(IRBlock* block) {
    Vec_init(block->code);
    block->start_addr = block->end_addr = 0;
    block->numinstr = 0;
    block->loop = false;
}
static inline void irblock_free(IRBlock* block) {
    Vec_free(block->code);
}
static inline u32 irblock_write(IRBlock* block, IRInstr instr) {
    return Vec_push(block->code, instr);
}

bool iropc_hasresult(IROpcode opc);
bool iropc_iscallback(IROpcode opc);
bool iropc_ispure(IROpcode opc);

void ir_interpret(IRBlock* block, ArmCore* cpu);

void ir_disasm_instr(IRInstr inst, int i);
void ir_disassemble(IRBlock* block);

#endif
