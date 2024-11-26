#ifndef IR_H
#define IR_H

#include <stdlib.h>

#include "../../common.h"
#include "../arm_core.h"

enum { NF, ZF, CF, VF, QF };

// each instruction can have up to 2 arguments and 1 result
// arguments can be
// v - variable (result of previous instruction) or 32 bit immediate
// i - only immediate value
// - - unused (represented as immediate 0)
// result is either
// r - result
// - - no result
// shown as <resulr><arg1><arg2>
typedef enum {
    // register load/store instructions
    IR_LOAD_REG,      // ri-
    IR_STORE_REG,     // -iv
    IR_LOAD_FLAG,     // ri-
    IR_STORE_FLAG,    // -iv
    IR_LOAD_REG_USR,  // ri-
    IR_STORE_REG_USR, // -iv
    IR_LOAD_CPSR,     // ri-
    IR_STORE_CPSR,    // -iv
    IR_LOAD_SPSR,     // ri-
    IR_STORE_SPSR,    // -iv
    IR_LOAD_THUMB,    // ri-
    IR_STORE_THUMB,   // -iv

    // memory access instructions
    IR_LOAD_MEM8,   // rv-
    IR_LOAD_MEMS8,  // rv-
    IR_LOAD_MEM16,  // rv-
    IR_LOAD_MEMS16, // rv-
    IR_LOAD_MEM32,  // rv-
    IR_STORE_MEM8,  // -vv
    IR_STORE_MEM16, // -vv
    IR_STORE_MEM32, // -vv

    // vfp instructions
    // TODO: integrate this into the jit properly
    IR_VFP_DATA_PROC, // -i-
    IR_VFP_LOAD_MEM,  // -iv
    IR_VFP_STORE_MEM, // -iv
    IR_VFP_READ,      // ri-
    IR_VFP_WRITE,     // -iv
    // these must always come in pairs
    IR_VFP_READ64L,  // ri-
    IR_VFP_READ64H,  // ri-
    IR_VFP_WRITE64L, // -iv
    IR_VFP_WRITE64H, // -iv

    // cp15 instructions
    IR_CP15_READ,  // ri-
    IR_CP15_WRITE, // -iv

    IR_SETC, // --v, sets the host carry flag

    // jump instructions
    IR_JZ,    // -vi
    IR_JNZ,   // -vi
    IR_JELSE, // --i, jumps if the last jump was not taken

    // special instructions
    IR_MODESWITCH, // -i-
    IR_EXCEPTION,  // -ii
    IR_WFE,        // ---

    // special control instructions
    IR_BEGIN,    // ---, always the first instruction
    IR_END_RET,  // ---, always returns to dispatcher
    IR_END_LINK, // -ii, jumps to the next block
    IR_END_LOOP, // ---, jumps to the beginning of the same block

    // arithmetic and logic instructions

    IR_NOP, // ---
    IR_MOV, // r-v

    // logic instructions
    IR_AND, // rvv
    IR_OR,  // rvv
    IR_XOR, // rvv
    IR_NOT, // r-v

    // shifts/rotates
    IR_LSL, // rvv
    IR_LSR, // rvv
    IR_ASR, // rvv
    IR_ROR, // rvv
    IR_RRC, // rv-, rotates with the host carry

    // add/subtract, these use arm carry semantics
    IR_ADD, // rvv
    IR_SUB, // rvv
    IR_ADC, // rvv, adds with host carry
    IR_SBC, // rvv, subtracts with host carry

    // special math instructions
    IR_MUL,   // rvv
    IR_SMULH, // rvv, top half of s32*s32
    IR_UMULH, // rvv, top half of u32*u32
    IR_SMULW, // rvv, "middle" half of s32*s32
    IR_CLZ,   // r-v
    IR_REV,   // r-v
    IR_REV16, // r-v
    IR_USAT,  // riv

    // media instructions
    IR_MEDIA_UADD8,  // rvv
    IR_MEDIA_UQSUB8, // rvv
    IR_MEDIA_SEL,    // rvv

    // flag instructions, these always appear in groups of alternating get flag
    // + store flag
    // immediately after the alu operation
    IR_GETN, // r-v
    IR_GETZ, // r-v
    IR_GETC, // r-v, gets the carry from last add/sub
    IR_GETV, // r-v, gets overflow from last add/sub

    IR_GETCIFZ, // rvv, if the first operand is 0 get the host carry flag, else
                // get second operand

    IR_PCMASK, // rv-, generates alignment mask from the given thumb status

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
