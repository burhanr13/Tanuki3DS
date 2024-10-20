#ifndef THUMB_H
#define THUMB_H

#include "../common.h"
#include "arm.h"

typedef union {
    u16 h;
    struct {
        u16 n0 : 4;
        u16 n1 : 4;
        u16 n2 : 4;
        u16 n3 : 4;
    };
    struct {
        u16 rd : 3;
        u16 rs : 3;
        u16 offset : 5;
        u16 op : 2;
        u16 c1 : 3; // 000
    } shift;
    struct {
        u16 rd : 3;
        u16 rs : 3;
        u16 op2 : 3;
        u16 op : 1;
        u16 i : 1;
        u16 c1 : 5; // 00011
    } add;
    struct {
        u16 offset : 8;
        u16 rd : 3;
        u16 op : 2;
        u16 c1 : 3; // 001
    } alu_imm;
    struct {
        u16 rd : 3;
        u16 rs : 3;
        u16 opcode : 4;
        u16 c1 : 6; // 010000
    } alu;
    struct {
        u16 rd : 3;
        u16 rs : 3;
        u16 h2 : 1;
        u16 h1 : 1;
        u16 op : 2;
        u16 c1 : 6; // 010001
    } hi_ops;
    struct {
        u16 offset : 8;
        u16 rd : 3;
        u16 c1 : 5; // 01001
    } ld_pc;
    struct {
        u16 rd : 3;
        u16 rb : 3;
        u16 ro : 3;
        u16 c2 : 1; // 0
        u16 b : 1;
        u16 l : 1;
        u16 c1 : 4; // 0101
    } ldst_reg;
    struct {
        u16 rd : 3;
        u16 rb : 3;
        u16 ro : 3;
        u16 c2 : 1; // 1
        u16 s : 1;
        u16 h : 1;
        u16 c1 : 4; // 0101
    } ldst_s;
    struct {
        u16 rd : 3;
        u16 rb : 3;
        u16 offset : 5;
        u16 l : 1;
        u16 b : 1;
        u16 c1 : 3; // 011
    } ldst_imm;
    struct {
        u16 rd : 3;
        u16 rb : 3;
        u16 offset : 5;
        u16 l : 1;
        u16 c1 : 4; // 1000
    } ldst_h;
    struct {
        u16 offset : 8;
        u16 rd : 3;
        u16 l : 1;
        u16 c1 : 4; // 1001
    } ldst_sp;
    struct {
        u16 offset : 8;
        u16 rd : 3;
        u16 sp : 1;
        u16 c1 : 4; // 1010
    } ld_addr;
    struct {
        u16 offset : 7;
        u16 s : 1;
        u16 c1 : 8; // 10110000
    } add_sp;
    struct {
        u16 rlist : 8;
        u16 r : 1;
        u16 c2 : 2; // 10
        u16 l : 1;
        u16 c1 : 4; // 1011
    } push_pop;
    struct {
        u16 rlist : 8;
        u16 rb : 3;
        u16 l : 1;
        u16 c1 : 4; // 1100
    } ldst_m;
    struct {
        u16 offset : 8;
        u16 cond : 4;
        u16 c1 : 4; // 1101
    } b_cond;
    struct {
        u16 arg : 8;
        u16 c1 : 8; // 11011111
    } swi;
    struct {
        u16 offset : 11;
        u16 c1 : 5; // 11100
    } branch;
    struct {
        u16 offset : 11;
        u16 h : 1;
        u16 h1 : 1;
        u16 c1 : 3; // 111
    } branch_l;
} ThumbInstr;

extern ArmInstr thumb_lookup[BIT(16)];

void thumb_generate_lookup();

ArmInstr thumb_decode_instr(ThumbInstr instr);

void thumb_disassemble(ThumbInstr instr, u32 addr, FILE* out);

#endif