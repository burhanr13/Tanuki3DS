#ifndef __RAS_H
#define __RAS_H

#include <stddef.h>
#include <stdint.h>

#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define s32 int32_t
#define u64 uint64_t

typedef struct _rasBlock rasBlock;

typedef struct _rasSymbol* rasLabel;

typedef enum {
    RAS_OK,
    RAS_ERR_CODE_SIZE,
    RAS_ERR_BAD_R31,
    RAS_ERR_BAD_IMM,
    RAS_ERR_BAD_CONST,
    RAS_ERR_UNDEF_LABEL,
    RAS_ERR_BAD_LABEL,

    RAS_ERR_MAX
} rasError;

typedef enum {
    RAS_PATCH_ABS64,
    RAS_PATCH_REL26,
    RAS_PATCH_REL19,
    RAS_PATCH_REL21,
    RAS_PATCH_PGREL21,
    RAS_PATCH_PGOFF12,
} rasPatchType;

extern char* rasErrorStrings[];

typedef struct {
    u8 idx : 5;
    u8 isSp : 1;
} rasReg;

typedef struct {
    u8 idx : 5;
} rasVReg;

typedef struct {
    u8 amt : 6;
    u8 type : 2;
} rasShift;

typedef struct {
    u8 amt : 3;
    u8 type : 3;
    u8 invalid : 1;
} rasExtend;

typedef void (*rasErrorCallback)(rasError, void*);

void rasSetErrorCallback(rasErrorCallback cb, void* userdata);

rasBlock* rasCreate(size_t initialSize);
void rasDestroy(rasBlock* ctx);

void rasReady(rasBlock* ctx);
void rasUnready(rasBlock* ctx);
void* rasGetCode(rasBlock* ctx);
size_t rasGetSize(rasBlock* ctx);

void rasAssert(int condition, rasError err);

rasLabel rasDeclareLabel(rasBlock* ctx);
rasLabel rasDefineLabel(rasBlock* ctx, rasLabel l);
rasLabel rasDefineLabelExternal(rasLabel l, void* addr);
void* rasGetLabelAddr(rasBlock* ctx, rasLabel l);

void rasAddPatch(rasBlock* ctx, rasPatchType type, rasLabel l);

int rasGenerateLogicalImm(u64 imm, u32 sf, u32* immr, u32* imms, u32* n);

void rasEmitWord(rasBlock* ctx, u32 w);
void rasEmitDword(rasBlock* ctx, u64 d);

void rasAlign(rasBlock* ctx, size_t alignment);

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
    rasEmitDword(ctx, 0);
}

__RAS_EMIT_DECL(AddSubImm, u32 sf, u32 op, u32 s, rasShift shift, u32 imm12,
                rasReg rn, rasReg rd) {
    RAS_CHECKR31(rd, !s);
    RAS_CHECKR31(rn, 1);
    rasAssert(shift.type == 0, RAS_ERR_BAD_CONST);
    rasAssert(shift.amt == 0 || shift.amt == 12, RAS_ERR_BAD_CONST);
    rasAssert(RAS_ISNBITSU(imm12, 12), RAS_ERR_BAD_IMM);
    u32 sh = shift.amt == 12;
    rasEmitWord(ctx, rd.idx | rn.idx << 5 | imm12 << 10 | sh << 22 | s << 29 |
                         op << 30 | sf << 31 | 0x11000000);
}

__RAS_EMIT_DECL(AddSubExtendedReg, u32 sf, u32 op, u32 s, rasExtend ext,
                rasReg rm, rasReg rn, rasReg rd) {
    RAS_CHECKR31(rd, !s);
    RAS_CHECKR31(rn, 1);
    RAS_CHECKR31(rm, 0);
    rasAssert(ext.amt <= 4 && !ext.invalid, RAS_ERR_BAD_CONST);
    rasEmitWord(ctx, rd.idx | rn.idx << 5 | ext.amt << 10 | ext.type << 13 |
                         rm.idx << 16 | s << 29 | op << 30 | sf << 31 |
                         0x0b200000);
}

__RAS_EMIT_DECL(AddSubShiftedReg, u32 sf, u32 op, u32 s, rasShift shift,
                rasReg rm, rasReg rn, rasReg rd) {
    if (rd.isSp || rn.isSp) {
        rasEmitAddSubExtendedReg(ctx, sf, op, s,
                                 (rasExtend) {shift.amt, sf ? 3 : 2,
                                              shift.type != 0 || shift.amt > 4},
                                 rm, rn, rd);
        return;
    }
    RAS_CHECKR31(rm, 0);
    if (!sf) rasAssert(!(shift.amt & RAS_BIT(5)), RAS_ERR_BAD_IMM);
    rasEmitWord(ctx, rd.idx | rn.idx << 5 | shift.amt << 10 | rm.idx << 16 |
                         shift.type << 22 | s << 29 | op << 30 | sf << 31 |
                         0x0b000000);
}

__RAS_EMIT_DECL(AddSubCarry, u32 sf, u32 op, u32 s, rasReg rm, rasReg rn,
                rasReg rd) {

    RAS_CHECKR31(rd, 0);
    RAS_CHECKR31(rn, 0);
    RAS_CHECKR31(rm, 0);
    rasEmitWord(ctx, rd.idx | rn.idx << 5 | rm.idx << 16 | s << 29 | op << 30 |
                         sf << 31 | 0x1a000000);
}

__RAS_EMIT_DECL(LogicalImm, u32 sf, u32 opc, u64 imm, rasReg rn, rasReg rd) {
    RAS_CHECKR31(rd, opc != 3);
    RAS_CHECKR31(rn, 0);
    u32 immr, imms, n;
    if (!rasGenerateLogicalImm(imm, sf, &immr, &imms, &n)) {
        rasAssert(0, RAS_ERR_BAD_IMM);
    }
    rasEmitWord(ctx, rd.idx | rn.idx << 5 | imms << 10 | immr << 16 | n << 22 |
                         opc << 29 | sf << 31 | 0x12000000);
}

__RAS_EMIT_DECL(LogicalReg, u32 sf, u32 opc, u32 n, rasShift shift, rasReg rm,
                rasReg rn, rasReg rd) {
    RAS_CHECKR31(rd, 0);
    RAS_CHECKR31(rn, 0);
    RAS_CHECKR31(rm, 0);
    rasEmitWord(ctx, rd.idx | rn.idx << 5 | shift.amt << 10 | rm.idx << 16 |
                         n << 21 | shift.type << 22 | opc << 29 | sf << 31 |
                         0x0a000000);
}

__RAS_EMIT_DECL(DataProc1Source, u32 sf, u32 s, u32 opcode2, u32 opcode,
                rasReg rn, rasReg rd) {
    RAS_CHECKR31(rd, 0);
    RAS_CHECKR31(rn, 0);
    rasEmitWord(ctx, rd.idx | rn.idx << 5 | opcode << 10 | opcode2 << 16 |
                         s << 29 | sf << 31 | 0x5ac00000);
}

__RAS_EMIT_DECL(DataProc2Source, u32 sf, u32 s, rasReg rm, u32 opcode,
                rasReg rn, rasReg rd) {
    RAS_CHECKR31(rd, 0);
    RAS_CHECKR31(rn, 0);
    RAS_CHECKR31(rm, 0);
    rasEmitWord(ctx, rd.idx | rn.idx << 5 | opcode << 10 | rm.idx << 16 |
                         s << 29 | sf << 31 | 0x1ac00000);
}

__RAS_EMIT_DECL(DataProc3Source, u32 sf, u32 op54, u32 op31, rasReg rm, u32 o0,
                rasReg ra, rasReg rn, rasReg rd) {
    RAS_CHECKR31(rd, 0);
    RAS_CHECKR31(rn, 0);
    RAS_CHECKR31(rm, 0);
    RAS_CHECKR31(ra, 0);
    rasEmitWord(ctx, rd.idx | rn.idx << 5 | ra.idx << 10 | o0 << 15 |
                         rm.idx << 16 | op31 << 21 | op54 << 29 | sf << 31 |
                         0x1b000000);
}

__RAS_EMIT_DECL(CondSelect, u32 sf, u32 op, u32 s, rasReg rm, u32 cond, u32 op2,
                rasReg rn, rasReg rd) {

    RAS_CHECKR31(rd, 0);
    RAS_CHECKR31(rn, 0);
    RAS_CHECKR31(rm, 0);
    rasEmitWord(ctx, rd.idx | rn.idx << 5 | op2 << 10 | cond << 12 |
                         rm.idx << 16 | s << 29 | op << 30 | sf << 31 |
                         0x1a800000);
}

__RAS_EMIT_DECL(PCRelAddr, u32 op, rasLabel lab, rasReg rd) {
    RAS_CHECKR31(rd, 0);
    rasAddPatch(ctx, op ? RAS_PATCH_PGREL21 : RAS_PATCH_REL21, lab);
    rasEmitWord(ctx, rd.idx | op << 31 | 0x10000000);
}

__RAS_EMIT_DECL(Bitfield, u32 sf, u32 opc, u32 n, u32 immr, u32 imms, rasReg rn,
                rasReg rd) {
    RAS_CHECKR31(rd, 0);
    RAS_CHECKR31(rn, 0);
    rasAssert(RAS_ISNBITSU(immr, 6), RAS_ERR_BAD_IMM);
    rasAssert(RAS_ISNBITSU(imms, 6), RAS_ERR_BAD_IMM);
    if (!sf)
        rasAssert(!(imms & RAS_BIT(5)) && !(immr & RAS_BIT(5)),
                  RAS_ERR_BAD_IMM);
    rasEmitWord(ctx, rd.idx | rn.idx << 5 | imms << 10 | immr << 16 | n << 22 |
                         opc << 29 | sf << 31 | 0x13000000);
}

__RAS_EMIT_DECL(Extract, u32 sf, u32 op21, u32 n, u32 o0, rasReg rm, u32 imms,
                rasReg rn, rasReg rd) {
    RAS_CHECKR31(rd, 0);
    RAS_CHECKR31(rn, 0);
    RAS_CHECKR31(rm, 0);
    rasAssert(RAS_ISNBITSU(imms, 6), RAS_ERR_BAD_IMM);
    if (!sf) rasAssert(!(imms & RAS_BIT(5)), RAS_ERR_BAD_IMM);
    rasEmitWord(ctx, rd.idx | rn.idx << 5 | imms << 10 | rm.idx << 16 |
                         o0 << 21 | n << 22 | op21 << 29 | sf << 31 |
                         0x13800000);
}

__RAS_EMIT_DECL(MoveWide, u32 sf, u32 opc, rasShift shift, u32 imm16,
                rasReg rd) {
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
    rasEmitWord(ctx, rd.idx | imm16 << 5 | hw << 21 | opc << 29 | sf << 31 |
                         0x12800000);
}

__RAS_EMIT_DECL(LoadStoreImmOff, u32 size, u32 vr, u32 opc, u32 imm, u32 mod,
                rasReg rn, rasReg rt) {
    RAS_CHECKR31(rt, 0);
    RAS_CHECKR31(rn, 1);
    u32 scale = (vr && (opc & 2)) ? 4 : size;
    if (mod == 0 && RAS_ISLOWBITS0(imm, scale) &&
        RAS_ISNBITSU(imm >> scale, 12)) {
        imm >>= scale;
        rasEmitWord(ctx, rt.idx | rn.idx << 5 | imm << 10 | opc << 22 |
                             vr << 26 | size << 30 | 0x39000000);
    } else {
        rasAssert(RAS_ISNBITSS(imm, 9), RAS_ERR_BAD_IMM);
        imm &= RAS_MASK(9);
        rasEmitWord(ctx, rt.idx | rn.idx << 5 | mod << 10 | imm << 12 |
                             opc << 22 | vr << 26 | size << 30 | 0x38000000);
    }
}

__RAS_EMIT_DECL(LoadStoreRegOff, u32 size, u32 vr, u32 opc, rasReg rm,
                rasExtend ext, rasReg rn, rasReg rt) {
    RAS_CHECKR31(rt, 0);
    RAS_CHECKR31(rn, 1);
    RAS_CHECKR31(rm, 0);
    rasAssert(!ext.invalid, RAS_ERR_BAD_CONST);
    rasAssert(ext.type & 2, RAS_ERR_BAD_CONST);
    u32 scale = (vr && (opc & 2)) ? 4 : size;
    rasAssert(ext.amt == 0 || ext.amt == scale, RAS_ERR_BAD_CONST);
    u32 s = ext.amt != 0;
    rasEmitWord(ctx, rt.idx | rn.idx << 5 | s << 12 | ext.type << 13 |
                         rm.idx << 16 | opc << 22 | vr << 26 | size << 30 |
                         0x38200800);
}

__RAS_EMIT_DECL(LoadLiteral, u32 opc, u32 vr, rasLabel l, rasReg rt) {
    RAS_CHECKR31(rt, 0);
    rasAddPatch(ctx, RAS_PATCH_REL19, l);
    rasEmitWord(ctx, rt.idx | vr << 26 | opc << 30 | 0x18000000);
}

__RAS_EMIT_DECL(LoadStorePair, u32 opc, u32 vr, u32 mod, u32 l, s32 imm,
                rasReg rt2, rasReg rn, rasReg rt) {
    RAS_CHECKR31(rt, 0);
    RAS_CHECKR31(rt2, 0);
    RAS_CHECKR31(rn, 1);
    u32 size = vr ? (opc == 3 ? 4 : opc + 2) : (opc & 2) ? 3 : 2;
    rasAssert(RAS_ISLOWBITS0(imm, size), RAS_ERR_BAD_IMM);
    imm >>= size;
    rasAssert(RAS_ISNBITSS(imm, 7), RAS_ERR_BAD_IMM);
    imm &= RAS_MASK(7);
    rasEmitWord(ctx, rt.idx | rn.idx << 5 | rt2.idx << 10 | imm << 15 |
                         l << 22 | mod << 23 | vr << 26 | opc << 30 |
                         0x28000000);
}

__RAS_EMIT_DECL(BranchUncondImm, u32 op, rasLabel lab) {
    rasAddPatch(ctx, RAS_PATCH_REL26, lab);
    rasEmitWord(ctx, op << 31 | 0x14000000);
}

__RAS_EMIT_DECL(BranchCondImm, rasLabel lab, u32 o0, u32 cond) {
    rasAddPatch(ctx, RAS_PATCH_REL19, lab);
    rasEmitWord(ctx, cond | o0 << 4 | 0x54000000);
}

__RAS_EMIT_DECL(BranchCompImm, u32 sf, u32 op, rasLabel lab, rasReg rt) {
    rasAddPatch(ctx, RAS_PATCH_REL19, lab);
    rasEmitWord(ctx, rt.idx | op << 24 | sf << 31 | 0x34000000);
}

__RAS_EMIT_DECL(BranchReg, u32 opc, u32 op2, u32 op3, rasReg rn, u32 op4) {
    RAS_CHECKR31(rn, 0);
    rasEmitWord(ctx, op4 | rn.idx << 5 | op3 << 10 | op2 << 16 | opc << 21 |
                         0xd6000000);
}

__RAS_EMIT_DECL(Hint, u32 opc) {
    rasEmitWord(ctx, opc << 5 | 0xd503201f);
}

__RAS_EMIT_DECL(SystemRegMove, u32 l, u32 opc, rasReg rt) {
    rasEmitWord(ctx, rt.idx | opc << 5 | l << 21 | 0xd5100000);
}

static inline int rasGenerateFPImm(float fimm, u8* imm8) {
    u32 imm = *(u32*) &fimm;
    u32 sgn = imm >> 31;
    u32 exp = (imm >> 23) & 0xff;
    u32 mant = imm & RAS_MASK(23);
    if (!RAS_ISLOWBITS0(mant, 19)) return 0;
    mant >>= 19;
    if (!(exp >> 2 == 0x1f || exp >> 2 == 0x20)) return 0;
    exp &= 7;
    *imm8 = sgn << 7 | exp << 4 | mant;
    return 1;
}

__RAS_EMIT_DECL(FPMovImm, u32 m, u32 s, u32 ftype, float fimm, u32 imm5,
                rasVReg rd) {
    u8 imm8;
    if (!rasGenerateFPImm(fimm, &imm8)) rasAssert(0, RAS_ERR_BAD_IMM);
    rasEmitWord(ctx, rd.idx | imm5 << 5 | imm8 << 13 | ftype << 22 | s << 29 |
                         m << 31 | 0x1e201000);
}

__RAS_EMIT_DECL(FPDataProc1Source, u32 m, u32 s, u32 ftype, u32 opcode,
                rasVReg rn, rasVReg rd) {
    rasEmitWord(ctx, rd.idx | rn.idx << 5 | opcode << 15 | ftype << 22 |
                         s << 29 | m << 31 | 0x1e204000);
}

__RAS_EMIT_DECL(FPCompare, u32 m, u32 s, u32 ftype, rasVReg rm, u32 op,
                rasVReg rn, u32 opcode2) {
    rasEmitWord(ctx, opcode2 | rn.idx << 5 | op << 14 | rm.idx << 16 |
                         ftype << 22 | s << 29 | m << 31 | 0x1e202000);
}

__RAS_EMIT_DECL(FPDataProc2Source, u32 m, u32 s, u32 ftype, rasVReg rm,
                u32 opcode, rasVReg rn, rasVReg rd) {
    rasEmitWord(ctx, rd.idx | rn.idx << 5 | opcode << 12 | rm.idx << 16 |
                         ftype << 22 | s << 29 | m << 31 | 0x1e200800);
}

__RAS_EMIT_DECL(FPDataProc3Source, u32 m, u32 s, u32 ftype, u32 o1, rasVReg rm,
                u32 o0, rasVReg ra, rasVReg rn, rasVReg rd) {
    rasEmitWord(ctx, rd.idx | rn.idx << 5 | ra.idx << 10 | o0 << 15 |
                         rm.idx << 16 | o1 << 21 | ftype << 22 | s << 29 |
                         m << 31 | 0x1f000000);
}

__RAS_EMIT_DECL(FPConvertInt, u32 sf, u32 s, u32 ftype, u32 rmode, u32 opcode,
                rasVReg rn, rasVReg rd) {
    rasEmitWord(ctx, rd.idx | rn.idx << 5 | opcode << 16 | rmode << 19 |
                         ftype << 22 | s << 29 | sf << 31 | 0x1e200000);
}

#undef RAS_BIT
#undef RAS_MASK
#undef RAS_ISNBITSU
#undef RAS_ISNBITSS
#undef RAS_ISLOWBITS0
#undef RAS_CHECKR31

void rasEmitPseudoAddSubImm(rasBlock* ctx, u32 sf, u32 op, u32 s, rasReg rd,
                            rasReg rn, u64 imm, rasReg rtmp);
void rasEmitPseudoLogicalImm(rasBlock* ctx, u32 sf, u32 opc, rasReg rd,
                             rasReg rn, u64 imm, rasReg rtmp);
void rasEmitPseudoMovImm(rasBlock* ctx, u32 sf, rasReg rd, u64 imm);
void rasEmitPseudoMovReg(rasBlock* ctx, u32 sf, rasReg rd, rasReg rm);
void rasEmitPseudoShiftImm(rasBlock* ctx, u32 sf, u32 type, rasReg rd,
                           rasReg rn, u32 imm);
void rasEmitPseudoPCRelAddrLong(rasBlock* ctx, rasReg rd, rasLabel lab);

#undef u8
#undef u16
#undef u32
#undef s32
#undef u64

#endif

#ifdef RAS_MACROS
#include "ras_macros.h"
#endif

#ifdef RAS_IMPL
#include "ras_impl.h"
#endif
