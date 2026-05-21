#ifndef __RAS_X64_H
#define __RAS_X64_H

#include "ras.h"

#define bool _Bool
#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define s32 int32_t
#define u64 uint64_t
#define s64 int64_t

typedef struct {
    u8 idx : 4;
    u8 h : 1;
} rasX64Reg;

typedef struct {
    u8 idx : 4;
} rasX64Xmm;

typedef struct {
    union {
        s64 disp;
        rasLabel lab;
    };
    rasX64Reg base;
    rasX64Reg index;
    int scale;
    bool nobase : 1;
    bool noindex : 1;
    bool rip : 1;
} rasX64Mem;

typedef struct {
    bool isMem;
    union {
        rasX64Reg r;
        rasX64Mem m;
    };
} rasX64Op;

void rasEmitSizePrefixes(rasBlock* ctx, int sz, rasX64Reg r, rasX64Op m,
                         bool useM);
void rasEmitSizePrefixesSSE(rasBlock* ctx, int sz, bool w, rasX64Xmm r,
                          rasX64Op m, bool useM);
void rasEmitOpcode(rasBlock* ctx, u32 opcode);
void rasEmitMemOp(rasBlock* ctx, u8 r, rasX64Op m, int immBytes);

#define __RAS_EMIT_DECL(name, ...)                                             \
    static inline void rasEmit##name(rasBlock* ctx, __VA_ARGS__)

__RAS_EMIT_DECL(AbsAddr, rasLabel l) {
    rasAddPatch(ctx, RAS_PATCH_ABS64, l);
    rasEmit64(ctx, 0);
}

__RAS_EMIT_DECL(OpRM, int sz, u32 opc, rasX64Reg op1, rasX64Op op2) {
    rasEmitSizePrefixes(ctx, sz, op1, op2, 1);
    rasEmitOpcode(ctx, opc | (sz != 0));
    rasEmitMemOp(ctx, op1.idx, op2, 0);
}

__RAS_EMIT_DECL(OpRM2, int sz, u32 opc, rasX64Reg op1, rasX64Op op2) {
    rasEmitSizePrefixes(ctx, sz, op1, op2, 1);
    rasEmitOpcode(ctx, opc);
    rasEmitMemOp(ctx, op1.idx, op2, 0);
}

__RAS_EMIT_DECL(OpRMI, int sz, u32 opc, rasX64Reg op1, rasX64Op op2, s64 op3) {
    rasEmitSizePrefixes(ctx, sz, op1, op2, 1);
    rasEmitOpcode(ctx, opc);
    rasEmitMemOp(ctx, op1.idx, op2, sz == 3 ? 4 : 1 << sz);
    switch (sz) {
        case 0: rasEmit8(ctx, op3); break;
        case 1: rasEmit16(ctx, op3); break;
        case 2: rasEmit32(ctx, op3); break;
        case 3:
            rasAssert(op3 == (s32) op3, RAS_ERR_BAD_IMM);
            rasEmit32(ctx, op3);
            break;
    }
}

__RAS_EMIT_DECL(OpM, int sz, u32 opc, u8 r, rasX64Op op1) {
    rasEmitSizePrefixes(ctx, sz, (rasX64Reg) {}, op1, 1);
    rasEmitOpcode(ctx, opc | (sz != 0));
    rasEmitMemOp(ctx, r, op1, 0);
}

__RAS_EMIT_DECL(OpMI, int sz, u32 opc, u8 r, rasX64Op op1, s64 op2) {
    rasEmitSizePrefixes(ctx, sz, (rasX64Reg) {}, op1, 1);
    rasEmitOpcode(ctx, opc | (sz != 0));
    rasEmitMemOp(ctx, r, op1, sz == 3 ? 4 : 1 << sz);
    switch (sz) {
        case 0: rasEmit8(ctx, op2); break;
        case 1: rasEmit16(ctx, op2); break;
        case 2: rasEmit32(ctx, op2); break;
        case 3:
            rasAssert(op2 == (s32) op2, RAS_ERR_BAD_IMM);
            rasEmit32(ctx, op2);
            break;
    }
}

__RAS_EMIT_DECL(OpO, int sz, u32 opc, rasX64Reg op1) {
    rasEmitSizePrefixes(ctx, sz, op1, (rasX64Op) {}, 0);
    rasEmitOpcode(ctx, opc | (op1.idx & 7));
}

__RAS_EMIT_DECL(OpOI, int sz, u32 opc, rasX64Reg op1, s64 op2) {
    rasEmitSizePrefixes(ctx, sz, op1, (rasX64Op) {}, 0);
    rasEmitOpcode(ctx, opc | (op1.idx & 7));
    switch (sz) {
        case 0: rasEmit8(ctx, op2); break;
        case 1: rasEmit16(ctx, op2); break;
        case 2: rasEmit32(ctx, op2); break;
        case 3: rasEmit64(ctx, op2); break;
    }
}

__RAS_EMIT_DECL(OpShiftI, int sz, u8 op, rasX64Op op1, u8 op2) {
    rasEmitSizePrefixes(ctx, sz, (rasX64Reg) {}, op1, 1);
    rasEmitOpcode(ctx, (op2 == 1 ? 0xd0 : 0xc0) | (sz != 0));
    rasEmitMemOp(ctx, op, op1, op2 != 1);
    if (op2 != 1) rasEmit8(ctx, op2);
}

__RAS_EMIT_DECL(OpVM, int sz, bool w, u32 opc, rasX64Xmm op1, rasX64Op op2) {
    rasEmitSizePrefixesSSE(ctx, sz, w, op1, op2, 1);
    rasEmitOpcode(ctx, opc);
    rasEmitMemOp(ctx, op1.idx, op2, 0);
}

__RAS_EMIT_DECL(OpVI, int sz, u32 opc, u8 r, rasX64Xmm op1, u8 op2) {
    rasEmitSizePrefixesSSE(ctx, sz, 0, op1, (rasX64Op) {}, 0);
    rasEmitOpcode(ctx, opc);
    rasEmit8(ctx, 0xc0 | r << 3 | (op1.idx & 7));
    rasEmit8(ctx, op2);
}

__RAS_EMIT_DECL(OpVMI, int sz, u32 opc, rasX64Xmm op1, rasX64Op op2, u8 op3) {
    rasEmitSizePrefixesSSE(ctx, sz, 0, op1, op2, 1);
    rasEmitOpcode(ctx, opc);
    rasEmitMemOp(ctx, op1.idx, op2, 1);
    rasEmit8(ctx, op3);
}

__RAS_EMIT_DECL(OpD, u32 opc, rasLabel dest, bool isShort) {
    rasEmitOpcode(ctx, opc);
    if (isShort) {
        rasAddPatchEx(ctx, RAS_PATCH_REL8, dest, -1);
        rasEmit8(ctx, 0);
    } else {
        rasAddPatchEx(ctx, RAS_PATCH_REL32, dest, -4);
        rasEmit32(ctx, 0);
    }
}

#undef bool
#undef u8
#undef u16
#undef u32
#undef s32
#undef u64
#undef s64

#include "ras_macros_x64.h"

#endif
