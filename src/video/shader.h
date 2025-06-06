#ifndef SHADER_H
#define SHADER_H

#include "common.h"

#define VSH_MAX 128

#define SHADER_CODE_SIZE 512
#define SHADER_OPDESC_SIZE 128

enum {
    PICA_ADD = 0x00,
    PICA_DP3 = 0x01,
    PICA_DP4 = 0x02,
    PICA_DPH = 0x03,
    PICA_DST = 0x04,
    PICA_EX2 = 0x05,
    PICA_LG2 = 0x06,
    PICA_MUL = 0x08,
    PICA_SGE = 0x09,
    PICA_SLT = 0x0a,
    PICA_FLR = 0x0b,
    PICA_MAX = 0x0c,
    PICA_MIN = 0x0d,
    PICA_RCP = 0x0e,
    PICA_RSQ = 0x0f,
    PICA_MOVA = 0x12,
    PICA_MOV = 0x13,
    PICA_DPHI = 0x18,
    PICA_DSTI = 0x19,
    PICA_SGEI = 0x1a,
    PICA_SLTI = 0x1b,
    PICA_BREAK = 0x20,
    PICA_NOP = 0x21,
    PICA_END = 0x22,
    PICA_BREAKC = 0x23,
    PICA_CALL = 0x24,
    PICA_CALLC = 0x25,
    PICA_CALLU = 0x26,
    PICA_IFU = 0x27,
    PICA_IFC = 0x28,
    PICA_LOOP = 0x29,
    PICA_EMIT = 0x2a,
    PICA_SETEMIT = 0x2b,
    PICA_JMPC = 0x2c,
    PICA_JMPU = 0x2d,
    PICA_CMP = 0x2e,
    PICA_MAD = 0x30,
};

typedef union {
    u32 w;
    struct {
        u32 desc : 7;
        u32 : 19;
        u32 opcode : 6;
    };
    struct {
        u32 desc : 7;
        u32 src2 : 5;
        u32 src1 : 7;
        u32 idx : 2;
        u32 dest : 5;
        u32 opcode : 6;
    } fmt1;
    struct {
        u32 desc : 7;
        u32 src2 : 7;
        u32 src1 : 5;
        u32 idx : 2;
        u32 dest : 5;
        u32 opcode : 6;
    } fmt1i;
    struct {
        u32 desc : 7;
        u32 src2 : 5;
        u32 src1 : 7;
        u32 idx : 2;
        u32 cmpy : 3;
        u32 cmpx : 3;
        u32 opcode : 5;
    } fmt1c;
    struct {
        u32 num : 8;
        u32 : 2;
        u32 dest : 12;
        u32 op : 2;
        u32 refy : 1;
        u32 refx : 1;
        u32 opcode : 6;
    } fmt2;
    struct {
        u32 num : 8;
        u32 : 2;
        u32 dest : 12;
        u32 c : 4;
        u32 opcode : 6;
    } fmt3;
    struct {
        u32 : 22;
        u32 inv : 1;
        u32 prim : 1;
        u32 vtxid : 2;
        u32 opcode : 6;
    } fmt4;
    struct {
        u32 desc : 5;
        u32 src3 : 5;
        u32 src2 : 7;
        u32 src1 : 5;
        u32 idx : 2;
        u32 dest : 5;
        u32 opcode : 3;
    } fmt5;
    struct {
        u32 desc : 5;
        u32 src3 : 7;
        u32 src2 : 5;
        u32 src1 : 5;
        u32 idx : 2;
        u32 dest : 5;
        u32 opcode : 3;
    } fmt5i;
} PICAInstr;

typedef union {
    u32 w;
    struct {
        u32 destmask : 4;
        u32 src1neg : 1;
        u32 src1swizzle : 8;
        u32 src2neg : 1;
        u32 src2swizzle : 8;
        u32 src3neg : 1;
        u32 src3swizzle : 8;
    };
} OpDesc;

typedef struct {
    PICAInstr* code;
    OpDesc* opdescs;
    u32 entrypoint;

    u32 outmap_mask;

    alignas(16) fvec4 v[16];
    alignas(16) fvec4 o[16];

    alignas(16) fvec4 r[16];

    fvec4* c;
    u8 (*i)[4];
    u16 b;

    s8 a[2];
    u8 al;
    bool cmp[2];

    struct {
        int emit_vtxid;
        bool emit_prim;
        bool emit_inv;

        fvec4 curvtx[4][16];

        Vector(fvec4[16]) outvtx;
    } gsh;

} ShaderUnit;

void pica_shader_exec(ShaderUnit* shu);

void pica_shader_disasm(ShaderUnit* shu);

void shader_write_outmap(ShaderUnit* shu, fvec4* out);

#endif
