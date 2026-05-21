#ifndef __RAS_A64_H
#define __RAS_A64_H

#include "ras.h"

#define bool _Bool
#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define s32 int32_t
#define u64 uint64_t

typedef struct {
    u8 idx : 5;
    u8 isSp : 1;
} rasA64Reg;

typedef struct {
    u8 idx : 5;
} rasA64VReg;

typedef struct {
    u8 amt : 6;
    u8 type : 2;
} rasA64Shift;

typedef struct {
    u8 amt : 3;
    u8 type : 3;
    u8 invalid : 1;
} rasA64Extend;

bool rasGenerateLogicalImm(u64 imm, u32 sf, u32* immr, u32* imms, u32* n);
bool rasGenerateFPImm(float fimm, u8* imm8);

#define RAS_BIT(b) (1 << (b))
#define RAS_MASK(b) (RAS_BIT(b) - 1)
#define RAS_ISNBITSU(n, b) ((u32) (n) >> (b) == 0)
#define RAS_ISNBITSS(n, b)                                                     \
    ((s32) (n) >> ((b) - 1) == 0 || (s32) (n) >> ((b) - 1) == -1)
#define RAS_ISLOWBITS0(n, b) (((n) & RAS_MASK(b)) == 0)
#define RAS_CHECKR31(r, canbesp)                                               \
    rasAssert(r.idx != 31 || r.isSp == (canbesp), RAS_ERR_BAD_R31)

#define __RAS_EMIT_DECL(name, ...)                                             \
    static inline void rasEmit##name(rasBlock* ctx, __VA_ARGS__)

__RAS_EMIT_DECL(AbsAddr, rasLabel l) {
    rasAddPatch(ctx, RAS_PATCH_ABS64, l);
    rasEmit64(ctx, 0);
}

__RAS_EMIT_DECL(AddSubImm, u32 sf, u32 op, u32 s, rasA64Shift shift, u32 imm12,
                rasA64Reg rn, rasA64Reg rd) {
    RAS_CHECKR31(rd, !s);
    RAS_CHECKR31(rn, 1);
    rasAssert(shift.type == 0, RAS_ERR_BAD_CONST);
    rasAssert(shift.amt == 0 || shift.amt == 12, RAS_ERR_BAD_CONST);
    rasAssert(RAS_ISNBITSU(imm12, 12), RAS_ERR_BAD_IMM);
    u32 sh = shift.amt == 12;
    rasEmit32(ctx, rd.idx | rn.idx << 5 | imm12 << 10 | sh << 22 | s << 29 |
                       op << 30 | sf << 31 | 0x11000000);
}

__RAS_EMIT_DECL(AddSubExtendedReg, u32 sf, u32 op, u32 s, rasA64Extend ext,
                rasA64Reg rm, rasA64Reg rn, rasA64Reg rd) {
    RAS_CHECKR31(rd, !s);
    RAS_CHECKR31(rn, 1);
    RAS_CHECKR31(rm, 0);
    rasAssert(ext.amt <= 4 && !ext.invalid, RAS_ERR_BAD_CONST);
    rasEmit32(ctx, rd.idx | rn.idx << 5 | ext.amt << 10 | ext.type << 13 |
                       rm.idx << 16 | s << 29 | op << 30 | sf << 31 |
                       0x0b200000);
}

__RAS_EMIT_DECL(AddSubShiftedReg, u32 sf, u32 op, u32 s, rasA64Shift shift,
                rasA64Reg rm, rasA64Reg rn, rasA64Reg rd) {
    if (rd.isSp || rn.isSp) {
        rasEmitAddSubExtendedReg(
            ctx, sf, op, s,
            (rasA64Extend) {shift.amt, sf ? 3 : 2,
                            shift.type != 0 || shift.amt > 4},
            rm, rn, rd);
        return;
    }
    RAS_CHECKR31(rm, 0);
    if (!sf) rasAssert(!(shift.amt & RAS_BIT(5)), RAS_ERR_BAD_IMM);
    rasEmit32(ctx, rd.idx | rn.idx << 5 | shift.amt << 10 | rm.idx << 16 |
                       shift.type << 22 | s << 29 | op << 30 | sf << 31 |
                       0x0b000000);
}

__RAS_EMIT_DECL(AddSubCarry, u32 sf, u32 op, u32 s, rasA64Reg rm, rasA64Reg rn,
                rasA64Reg rd) {

    RAS_CHECKR31(rd, 0);
    RAS_CHECKR31(rn, 0);
    RAS_CHECKR31(rm, 0);
    rasEmit32(ctx, rd.idx | rn.idx << 5 | rm.idx << 16 | s << 29 | op << 30 |
                       sf << 31 | 0x1a000000);
}

__RAS_EMIT_DECL(LogicalImm, u32 sf, u32 opc, u64 imm, rasA64Reg rn,
                rasA64Reg rd) {
    RAS_CHECKR31(rd, opc != 3);
    RAS_CHECKR31(rn, 0);
    u32 immr, imms, n;
    rasAssert(rasGenerateLogicalImm(imm, sf, &immr, &imms, &n),
              RAS_ERR_BAD_IMM);
    rasEmit32(ctx, rd.idx | rn.idx << 5 | imms << 10 | immr << 16 | n << 22 |
                       opc << 29 | sf << 31 | 0x12000000);
}

__RAS_EMIT_DECL(LogicalReg, u32 sf, u32 opc, u32 n, rasA64Shift shift,
                rasA64Reg rm, rasA64Reg rn, rasA64Reg rd) {
    RAS_CHECKR31(rd, 0);
    RAS_CHECKR31(rn, 0);
    RAS_CHECKR31(rm, 0);
    rasEmit32(ctx, rd.idx | rn.idx << 5 | shift.amt << 10 | rm.idx << 16 |
                       n << 21 | shift.type << 22 | opc << 29 | sf << 31 |
                       0x0a000000);
}

__RAS_EMIT_DECL(DataProc1Source, u32 sf, u32 s, u32 opcode2, u32 opcode,
                rasA64Reg rn, rasA64Reg rd) {
    RAS_CHECKR31(rd, 0);
    RAS_CHECKR31(rn, 0);
    rasEmit32(ctx, rd.idx | rn.idx << 5 | opcode << 10 | opcode2 << 16 |
                       s << 29 | sf << 31 | 0x5ac00000);
}

__RAS_EMIT_DECL(DataProc2Source, u32 sf, u32 s, rasA64Reg rm, u32 opcode,
                rasA64Reg rn, rasA64Reg rd) {
    RAS_CHECKR31(rd, 0);
    RAS_CHECKR31(rn, 0);
    RAS_CHECKR31(rm, 0);
    rasEmit32(ctx, rd.idx | rn.idx << 5 | opcode << 10 | rm.idx << 16 |
                       s << 29 | sf << 31 | 0x1ac00000);
}

__RAS_EMIT_DECL(DataProc3Source, u32 sf, u32 op54, u32 op31, rasA64Reg rm,
                u32 o0, rasA64Reg ra, rasA64Reg rn, rasA64Reg rd) {
    RAS_CHECKR31(rd, 0);
    RAS_CHECKR31(rn, 0);
    RAS_CHECKR31(rm, 0);
    RAS_CHECKR31(ra, 0);
    rasEmit32(ctx, rd.idx | rn.idx << 5 | ra.idx << 10 | o0 << 15 |
                       rm.idx << 16 | op31 << 21 | op54 << 29 | sf << 31 |
                       0x1b000000);
}

__RAS_EMIT_DECL(CondSelect, u32 sf, u32 op, u32 s, rasA64Reg rm, u32 cond,
                u32 op2, rasA64Reg rn, rasA64Reg rd) {

    RAS_CHECKR31(rd, 0);
    RAS_CHECKR31(rn, 0);
    RAS_CHECKR31(rm, 0);
    rasEmit32(ctx, rd.idx | rn.idx << 5 | op2 << 10 | cond << 12 |
                       rm.idx << 16 | s << 29 | op << 30 | sf << 31 |
                       0x1a800000);
}

__RAS_EMIT_DECL(PCRelAddr, u32 op, rasLabel lab, rasA64Reg rd) {
    RAS_CHECKR31(rd, 0);
    rasAddPatch(ctx, op ? RAS_PATCH_PGREL21 : RAS_PATCH_REL21, lab);
    rasEmit32(ctx, rd.idx | op << 31 | 0x10000000);
}

__RAS_EMIT_DECL(Bitfield, u32 sf, u32 opc, u32 n, u32 immr, u32 imms,
                rasA64Reg rn, rasA64Reg rd) {
    RAS_CHECKR31(rd, 0);
    RAS_CHECKR31(rn, 0);
    rasAssert(RAS_ISNBITSU(immr, 6), RAS_ERR_BAD_IMM);
    rasAssert(RAS_ISNBITSU(imms, 6), RAS_ERR_BAD_IMM);
    if (!sf)
        rasAssert(!(imms & RAS_BIT(5)) && !(immr & RAS_BIT(5)),
                  RAS_ERR_BAD_IMM);
    rasEmit32(ctx, rd.idx | rn.idx << 5 | imms << 10 | immr << 16 | n << 22 |
                       opc << 29 | sf << 31 | 0x13000000);
}

__RAS_EMIT_DECL(Extract, u32 sf, u32 op21, u32 n, u32 o0, rasA64Reg rm,
                u32 imms, rasA64Reg rn, rasA64Reg rd) {
    RAS_CHECKR31(rd, 0);
    RAS_CHECKR31(rn, 0);
    RAS_CHECKR31(rm, 0);
    rasAssert(RAS_ISNBITSU(imms, 6), RAS_ERR_BAD_IMM);
    if (!sf) rasAssert(!(imms & RAS_BIT(5)), RAS_ERR_BAD_IMM);
    rasEmit32(ctx, rd.idx | rn.idx << 5 | imms << 10 | rm.idx << 16 | o0 << 21 |
                       n << 22 | op21 << 29 | sf << 31 | 0x13800000);
}

__RAS_EMIT_DECL(MoveWide, u32 sf, u32 opc, rasA64Shift shift, u32 imm16,
                rasA64Reg rd) {
    RAS_CHECKR31(rd, 0);
    rasAssert(RAS_ISNBITSU(imm16, 16), RAS_ERR_BAD_IMM);
    rasAssert(shift.type == 0, RAS_ERR_BAD_CONST);
    if (sf) {
        rasAssert(shift.amt == 0 || shift.amt == 16 || shift.amt == 32 ||
                      shift.amt == 48,
                  RAS_ERR_BAD_CONST);
    } else {
        rasAssert(shift.amt == 0 || shift.amt == 16, RAS_ERR_BAD_CONST);
    }
    u32 hw = shift.amt / 16;
    rasEmit32(ctx, rd.idx | imm16 << 5 | hw << 21 | opc << 29 | sf << 31 |
                       0x12800000);
}

__RAS_EMIT_DECL(LoadStoreImmOff, u32 size, u32 vr, u32 opc, u32 imm, u32 mod,
                rasA64Reg rn, rasA64Reg rt) {
    RAS_CHECKR31(rt, 0);
    RAS_CHECKR31(rn, 1);
    u32 scale = (vr && (opc & 2)) ? 4 : size;
    if (mod == 0 && RAS_ISLOWBITS0(imm, scale) &&
        RAS_ISNBITSU(imm >> scale, 12)) {
        imm >>= scale;
        rasEmit32(ctx, rt.idx | rn.idx << 5 | imm << 10 | opc << 22 | vr << 26 |
                           size << 30 | 0x39000000);
    } else {
        rasAssert(RAS_ISNBITSS(imm, 9), RAS_ERR_BAD_IMM);
        imm &= RAS_MASK(9);
        rasEmit32(ctx, rt.idx | rn.idx << 5 | mod << 10 | imm << 12 |
                           opc << 22 | vr << 26 | size << 30 | 0x38000000);
    }
}

__RAS_EMIT_DECL(LoadStoreRegOff, u32 size, u32 vr, u32 opc, rasA64Reg rm,
                rasA64Extend ext, rasA64Reg rn, rasA64Reg rt) {
    RAS_CHECKR31(rt, 0);
    RAS_CHECKR31(rn, 1);
    RAS_CHECKR31(rm, 0);
    rasAssert(!ext.invalid, RAS_ERR_BAD_CONST);
    rasAssert(ext.type & 2, RAS_ERR_BAD_CONST);
    u32 scale = (vr && (opc & 2)) ? 4 : size;
    rasAssert(ext.amt == 0 || ext.amt == scale, RAS_ERR_BAD_CONST);
    u32 s = ext.amt != 0;
    rasEmit32(ctx, rt.idx | rn.idx << 5 | s << 12 | ext.type << 13 |
                       rm.idx << 16 | opc << 22 | vr << 26 | size << 30 |
                       0x38200800);
}

__RAS_EMIT_DECL(LoadLiteral, u32 opc, u32 vr, rasLabel l, rasA64Reg rt) {
    RAS_CHECKR31(rt, 0);
    rasAddPatch(ctx, RAS_PATCH_REL19, l);
    rasEmit32(ctx, rt.idx | vr << 26 | opc << 30 | 0x18000000);
}

__RAS_EMIT_DECL(LoadStorePair, u32 opc, u32 vr, u32 mod, u32 l, s32 imm,
                rasA64Reg rt2, rasA64Reg rn, rasA64Reg rt) {
    RAS_CHECKR31(rt, 0);
    RAS_CHECKR31(rt2, 0);
    RAS_CHECKR31(rn, 1);
    u32 size = vr ? (opc == 3 ? 4 : opc + 2) : (opc & 2) ? 3 : 2;
    rasAssert(RAS_ISLOWBITS0(imm, size), RAS_ERR_BAD_IMM);
    imm >>= size;
    rasAssert(RAS_ISNBITSS(imm, 7), RAS_ERR_BAD_IMM);
    imm &= RAS_MASK(7);
    rasEmit32(ctx, rt.idx | rn.idx << 5 | rt2.idx << 10 | imm << 15 | l << 22 |
                       mod << 23 | vr << 26 | opc << 30 | 0x28000000);
}

__RAS_EMIT_DECL(BranchUncondImm, u32 op, rasLabel lab) {
    rasAddPatch(ctx, RAS_PATCH_REL26, lab);
    rasEmit32(ctx, op << 31 | 0x14000000);
}

__RAS_EMIT_DECL(BranchCondImm, rasLabel lab, u32 o0, u32 cond) {
    rasAddPatch(ctx, RAS_PATCH_REL19, lab);
    rasEmit32(ctx, cond | o0 << 4 | 0x54000000);
}

__RAS_EMIT_DECL(BranchCompImm, u32 sf, u32 op, rasLabel lab, rasA64Reg rt) {
    rasAddPatch(ctx, RAS_PATCH_REL19, lab);
    rasEmit32(ctx, rt.idx | op << 24 | sf << 31 | 0x34000000);
}

__RAS_EMIT_DECL(BranchTestImm, u32 op, u32 b, rasLabel lab, rasA64Reg rt) {
    rasAddPatch(ctx, RAS_PATCH_REL14, lab);
    rasEmit32(ctx, rt.idx | (b & 0x1f) << 19 | op << 24 | (b >> 5) << 31 |
                       0x36000000);
}

__RAS_EMIT_DECL(BranchReg, u32 opc, u32 op2, u32 op3, rasA64Reg rn, u32 op4) {
    RAS_CHECKR31(rn, 0);
    rasEmit32(ctx, op4 | rn.idx << 5 | op3 << 10 | op2 << 16 | opc << 21 |
                       0xd6000000);
}

__RAS_EMIT_DECL(Hint, u32 opc) {
    rasEmit32(ctx, opc << 5 | 0xd503201f);
}

__RAS_EMIT_DECL(SystemRegMove, u32 l, u32 opc, rasA64Reg rt) {
    rasEmit32(ctx, rt.idx | opc << 5 | l << 21 | 0xd5100000);
}

__RAS_EMIT_DECL(FPMoveImm, u32 m, u32 s, u32 ftype, float fimm, u32 imm5,
                rasA64VReg rd) {
    u8 imm8;
    rasAssert(rasGenerateFPImm(fimm, &imm8), RAS_ERR_BAD_IMM);
    rasEmit32(ctx, rd.idx | imm5 << 5 | imm8 << 13 | ftype << 22 | s << 29 |
                       m << 31 | 0x1e201000);
}

__RAS_EMIT_DECL(FPDataProc1Source, u32 m, u32 s, u32 ftype, u32 opcode,
                rasA64VReg rn, rasA64VReg rd) {
    rasEmit32(ctx, rd.idx | rn.idx << 5 | opcode << 15 | ftype << 22 | s << 29 |
                       m << 31 | 0x1e204000);
}

__RAS_EMIT_DECL(FPCompare, u32 m, u32 s, u32 ftype, rasA64VReg rm, u32 op,
                rasA64VReg rn, u32 opcode2) {
    rasEmit32(ctx, opcode2 | rn.idx << 5 | op << 14 | rm.idx << 16 |
                       ftype << 22 | s << 29 | m << 31 | 0x1e202000);
}

__RAS_EMIT_DECL(FPDataProc2Source, u32 m, u32 s, u32 ftype, rasA64VReg rm,
                u32 opcode, rasA64VReg rn, rasA64VReg rd) {
    rasEmit32(ctx, rd.idx | rn.idx << 5 | opcode << 12 | rm.idx << 16 |
                       ftype << 22 | s << 29 | m << 31 | 0x1e200800);
}

__RAS_EMIT_DECL(FPDataProc3Source, u32 m, u32 s, u32 ftype, u32 o1,
                rasA64VReg rm, u32 o0, rasA64VReg ra, rasA64VReg rn,
                rasA64VReg rd) {
    rasEmit32(ctx, rd.idx | rn.idx << 5 | ra.idx << 10 | o0 << 15 |
                       rm.idx << 16 | o1 << 21 | ftype << 22 | s << 29 |
                       m << 31 | 0x1f000000);
}

__RAS_EMIT_DECL(FPConvertInt, u32 sf, u32 s, u32 ftype, u32 rmode, u32 opcode,
                rasA64VReg rn, rasA64VReg rd) {
    rasEmit32(ctx, rd.idx | rn.idx << 5 | opcode << 16 | rmode << 19 |
                       ftype << 22 | s << 29 | sf << 31 | 0x1e200000);
}

__RAS_EMIT_DECL(FPCondSelect, u32 m, u32 s, u32 ftype, rasA64VReg rm, u32 cond,
                rasA64VReg rn, rasA64VReg rd) {
    rasEmit32(ctx, rd.idx | rn.idx << 5 | cond << 12 | rm.idx << 16 |
                       ftype << 22 | s << 29 | m << 31 | 0x1e200c00);
}

__RAS_EMIT_DECL(AdvSIMDCopy, u32 q, u32 op, u32 imm5, u32 imm4, rasA64VReg rn,
                rasA64VReg rd) {
    rasAssert(RAS_ISNBITSU(imm5, 5) && RAS_ISNBITSU(imm4, 4),
              RAS_ERR_BAD_CONST);
    rasEmit32(ctx, rd.idx | rn.idx << 5 | imm4 << 11 | imm5 << 16 | op << 29 |
                       q << 30 | 0x0e000400);
}

__RAS_EMIT_DECL(AdvSIMDScalarPairwise, u32 u, u32 size, u32 opcode,
                rasA64VReg rn, rasA64VReg rd) {
    rasEmit32(ctx, rd.idx | rn.idx << 5 | opcode << 12 | size << 22 | u << 29 |
                       0x5e300800);
}

__RAS_EMIT_DECL(AdvSIMDScalar2Misc, u32 u, u32 size, u32 opcode, rasA64VReg rn,
                rasA64VReg rd) {
    rasEmit32(ctx, rd.idx | rn.idx << 5 | opcode << 12 | size << 22 | u << 29 |
                       0x5e200800);
}

__RAS_EMIT_DECL(AdvSIMD2Misc, u32 q, u32 u, u32 size, u32 opcode, rasA64VReg rn,
                rasA64VReg rd) {
    rasEmit32(ctx, rd.idx | rn.idx << 5 | opcode << 12 | size << 22 | u << 29 |
                       q << 30 | 0x0e200800);
}

__RAS_EMIT_DECL(AdvSIMD3Same, u32 q, u32 u, u32 size, rasA64VReg rm, u32 opcode,
                rasA64VReg rn, rasA64VReg rd) {
    rasEmit32(ctx, rd.idx | rn.idx << 5 | opcode << 11 | rm.idx << 16 |
                       size << 22 | u << 29 | q << 30 | 0x0e200400);
}

__RAS_EMIT_DECL(AdvSIMDModImmFloat, u32 q, u32 op, u32 cmode, u32 o2,
                float fimm, rasA64VReg rd) {
    u8 imm8;
    rasAssert(rasGenerateFPImm(fimm, &imm8), RAS_ERR_BAD_IMM);
    rasEmit32(ctx, rd.idx | (imm8 & 0x1f) << 5 | o2 << 11 | cmode << 12 |
                       (imm8 >> 5) << 16 | op << 29 | q << 30 | 0x0f000400);
}

#undef RAS_BIT
#undef RAS_MASK
#undef RAS_ISNBITSU
#undef RAS_ISNBITSS
#undef RAS_ISLOWBITS0
#undef RAS_CHECKR31

void rasEmitPseudoAddSubImm(rasBlock* ctx, u32 sf, u32 op, u32 s, rasA64Reg rd,
                            rasA64Reg rn, u64 imm, rasA64Reg rtmp);
void rasEmitPseudoLogicalImm(rasBlock* ctx, u32 sf, u32 opc, rasA64Reg rd,
                             rasA64Reg rn, u64 imm, rasA64Reg rtmp);
void rasEmitPseudoMovImm(rasBlock* ctx, u32 sf, rasA64Reg rd, u64 imm);
void rasEmitPseudoMovReg(rasBlock* ctx, u32 sf, rasA64Reg rd, rasA64Reg rm);
void rasEmitPseudoShiftImm(rasBlock* ctx, u32 sf, u32 type, rasA64Reg rd,
                           rasA64Reg rn, u32 imm);
void rasEmitPseudoPCRelAddrLong(rasBlock* ctx, rasA64Reg rd, rasLabel lab);

#undef bool
#undef u8
#undef u16
#undef u32
#undef s32
#undef u64

#include "ras_macros_a64.h"

#endif
