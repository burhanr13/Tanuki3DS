#ifndef ARM_H
#define ARM_H

#include <stdio.h>

#include "../common.h"

enum {
    C_EQ,
    C_NE,
    C_CS,
    C_CC,
    C_MI,
    C_PL,
    C_VS,
    C_VC,
    C_HI,
    C_LS,
    C_GE,
    C_LT,
    C_GT,
    C_LE,
    C_AL
};

enum {
    A_AND,
    A_EOR,
    A_SUB,
    A_RSB,
    A_ADD,
    A_ADC,
    A_SBC,
    A_RSC,
    A_TST,
    A_TEQ,
    A_CMP,
    A_CMN,
    A_ORR,
    A_MOV,
    A_BIC,
    A_MVN
};

enum { S_LSL, S_LSR, S_ASR, S_ROR };

enum {
    T_AND,
    T_EOR,
    T_LSL,
    T_LSR,
    T_ASR,
    T_ADC,
    T_SBC,
    T_ROR,
    T_TST,
    T_NEG,
    T_CMP,
    T_CMN,
    T_ORR,
    T_MUL,
    T_BIC,
    T_MVN
};

typedef union {
    u32 w;
    struct {
        u32 _b0_3 : 4;
        u32 declo : 4;
        u32 _b8_19 : 12;
        u32 dechi : 8;
        u32 cond : 4;
    };
    struct {
        u32 op2 : 12;
        u32 rd : 4;
        u32 rn : 4;
        u32 s : 1;
        u32 opcode : 4;
        u32 i : 1;
        u32 c1 : 2; // 00
        u32 cond : 4;
    } data_proc;
    struct {
        u32 op2 : 12;
        u32 rd : 4;
        u32 c : 1;
        u32 x : 1;
        u32 s : 1;
        u32 f : 1;
        u32 c3 : 1; // 0
        u32 op : 1;
        u32 p : 1;
        u32 c2 : 2; // 10
        u32 i : 1;
        u32 c1 : 2; // 00
        u32 cond : 4;
    } psr_trans;
    struct {
        u32 rm : 4;
        u32 c2 : 4; // 1001
        u32 rs : 4;
        u32 rn : 4;
        u32 rd : 4;
        u32 s : 1;
        u32 a : 1;
        u32 c1 : 6; // 000000
        u32 cond : 4;
    } multiply;
    struct {
        u32 rm : 4;
        u32 c2 : 4; // 1001
        u32 rs : 4;
        u32 rdlo : 4;
        u32 rdhi : 4;
        u32 s : 1;
        u32 a : 1;
        u32 u : 1;
        u32 c1 : 5; // 00001
        u32 cond : 4;
    } multiply_long;
    struct {
        u32 rm : 4;
        u32 c4 : 1; // 0
        u32 x : 1;
        u32 y : 1;
        u32 c3 : 1; // 1
        u32 rs : 4;
        u32 rn : 4;
        u32 rd : 4;
        u32 c2 : 1; // 0
        u32 op : 2;
        u32 c1 : 5; // 00010
    } multiply_short;
    struct {
        u32 rm : 4;
        u32 c4 : 4; // 1001
        u32 c3 : 4;
        u32 rd : 4;
        u32 rn : 4;
        u32 c2 : 2; // 00
        u32 b : 1;
        u32 c1 : 5; // 00010
        u32 cond : 4;
    } swap;
    struct {
        u32 rn : 4;
        u32 c4 : 1; // 1
        u32 l : 1;
        u32 c3 : 2; // 00
        u32 c2 : 12;
        u32 c1 : 8; // 00010010
        u32 cond : 4;
    } branch_exch;
    struct {
        u32 rm : 4;
        u32 c4 : 4; // 0001
        u32 c3 : 4;
        u32 rd : 4;
        u32 c2 : 4;
        u32 c1 : 8; // 00010110
        u32 cond : 4;
    } leading_zeros;
    struct {
        u32 rm : 4;
        u32 c4 : 4; // 0101
        u32 c3 : 4;
        u32 rd : 4;
        u32 rn : 4;
        u32 c2 : 1; // 0
        u32 op : 1;
        u32 d : 1;
        u32 c1 : 5; // 00010
        u32 cond : 4;
    } sat_arith;
    struct {
        u32 offlo : 4;
        u32 c3 : 1; // 1
        u32 h : 1;
        u32 s : 1;
        u32 c2 : 1; // 1
        u32 offhi : 4;
        u32 rd : 4;
        u32 rn : 4;
        u32 l : 1;
        u32 w : 1;
        u32 i : 1;
        u32 u : 1;
        u32 p : 1;
        u32 c1 : 3; // 000
        u32 cond : 4;
    } half_trans;
    struct {
        u32 offset : 12;
        u32 rd : 4;
        u32 rn : 4;
        u32 l : 1;
        u32 w : 1;
        u32 b : 1;
        u32 u : 1;
        u32 p : 1;
        u32 i : 1;
        u32 c1 : 2; // 01
        u32 cond : 4;
    } single_trans;
    struct {
        u32 rm : 4;
        u32 c2 : 1; // 1
        u32 p : 1;
        u32 x : 1;
        u32 shift : 5;
        u32 rd : 4;
        u32 rn : 4;
        u32 h : 1;
        u32 s : 1;
        u32 u : 1;
        u32 c1 : 5; // 01101
        u32 cond : 4;
    } pack_sat;
    struct {
        u32 rm : 4;
        u32 c2 : 1; // 1
        u32 op2 : 2;
        u32 b : 1;
        u32 rs : 4;
        u32 rd : 4;
        u32 rn : 4;
        u32 op1 : 2;
        u32 u : 1;
        u32 c1 : 5; // 01100
        u32 cond : 4;
    } parallel_arith;
    struct {
        u32 u2 : 4;
        u32 c2 : 1; // 1
        u32 u1 : 20;
        u32 c1 : 3; // 011
        u32 cond : 4;
    } undefined;
    struct {
        u32 rlist : 16;
        u32 rn : 4;
        u32 l : 1;
        u32 w : 1;
        u32 s : 1;
        u32 u : 1;
        u32 p : 1;
        u32 c1 : 3; // 100
        u32 cond : 4;
    } block_trans;
    struct {
        u32 offset : 24;
        u32 l : 1;
        u32 c1 : 3; // 101
        u32 cond : 4;
    } branch;
    struct {
        u32 crm : 4;
        u32 cp : 4;
        u32 cpnum : 4;
        u32 rdlo : 4;
        u32 rdhi : 4;
        u32 l : 1;
        u32 c1 : 7; // 1100010
        u32 cond : 4;
    } cp_double_reg_trans;
    struct {
        u32 offset : 8;
        u32 cpnum : 4;
        u32 crd : 4;
        u32 rn : 4;
        u32 l : 1;
        u32 w : 1;
        u32 n : 1;
        u32 u : 1;
        u32 p : 1;
        u32 c1 : 3; // 110
        u32 cond : 4;
    } cp_data_trans;
    struct {
        u32 crm : 4;
        u32 c2 : 1; // 0
        u32 cp : 3;
        u32 cpnum : 4;
        u32 crd : 4;
        u32 crn : 4;
        u32 cpopc : 4;
        u32 c1 : 4; // 1110
        u32 cond : 4;
    } cp_data_proc;
    struct {
        u32 crm : 4;
        u32 c2 : 1; // 1
        u32 cp : 3;
        u32 cpnum : 4;
        u32 rd : 4;
        u32 crn : 4;
        u32 l : 1;
        u32 cpopc : 3;
        u32 c1 : 4; // 1110
        u32 cond : 4;
    } cp_reg_trans;
    struct {
        u32 arg : 24;
        u32 c1 : 4; // 1111
        u32 cond : 4;
    } sw_intr;
} ArmInstr;

typedef enum {
    ARM_DATAPROC,
    ARM_PSRTRANS,
    ARM_MULTIPLY,
    ARM_MULTIPLYLONG,
    ARM_MULTIPLYSHORT,
    ARM_SWAP,
    ARM_BRANCHEXCH,
    ARM_LEADINGZEROS,
    ARM_SATARITH,
    ARM_HALFTRANS,
    ARM_SINGLETRANS,
    ARM_PACKSAT,
    ARM_PARALLELARITH,
    ARM_UNDEFINED,
    ARM_BLOCKTRANS,
    ARM_BRANCH,
    ARM_CPDOUBLEREGTRANS,
    ARM_CPDATATRANS,
    ARM_CPDATAPROC,
    ARM_CPREGTRANS,
    ARM_SWINTR,

    ARM_MAX
} ArmInstrFormat;

typedef struct _ArmCore ArmCore;

extern ArmInstrFormat arm_lookup[BIT(8)][BIT(4)];

void arm_generate_lookup();
ArmInstrFormat arm_decode_instr(ArmInstr instr);

void arm_exec_instr(ArmCore* cpu);

typedef void (*ArmExecFunc)(ArmCore*, ArmInstr);

#define DECL_ARM_EXEC(f) void exec_arm_##f(ArmCore* cpu, ArmInstr instr)

DECL_ARM_EXEC(data_proc);
DECL_ARM_EXEC(psr_trans);
DECL_ARM_EXEC(multiply);
DECL_ARM_EXEC(multiply_long);
DECL_ARM_EXEC(multiply_short);
DECL_ARM_EXEC(swap);
DECL_ARM_EXEC(branch_exch);
DECL_ARM_EXEC(leading_zeros);
DECL_ARM_EXEC(sat_arith);
DECL_ARM_EXEC(half_trans);
DECL_ARM_EXEC(single_trans);
DECL_ARM_EXEC(undefined);
DECL_ARM_EXEC(block_trans);
DECL_ARM_EXEC(branch);
DECL_ARM_EXEC(cp_reg_trans);
DECL_ARM_EXEC(sw_intr);

void arm_disassemble(ArmInstr instr, u32 addr, FILE* out);

#endif
