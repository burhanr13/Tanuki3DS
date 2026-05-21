#include "ras_x64.h"

#include <stdbool.h>

typedef int8_t s8;
typedef uint8_t u8;
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;
typedef int64_t s64;

#define REX(w, r, x, b) (0x40 | (w) << 3 | (r) << 2 | (x) << 1 | (b))
#define MODRM(mod, r, m) ((mod) << 6 | (r) << 3 | (m))
#define SIB(s, i, b) ((s) << 6 | (i) << 3 | (b))

void rasEmitSizePrefixes(rasBlock* ctx, int sz, rasX64Reg r, rasX64Op m,
                         bool useM) {
    u8 rex;
    if (useM) {
        if (m.isMem) {
            rex = REX(sz == 3, r.idx > 7, m.m.index.idx > 7, m.m.base.idx > 7);
        } else {
            rex = REX(sz == 3, r.idx > 7, 0, m.r.idx > 7);
        }
    } else {
        rex = REX(sz == 3, 0, 0, r.idx > 7);
    }
    bool uniformByte = false;
    if (sz == 0) {
        if (4 <= r.idx && r.idx <= 7) {
            uniformByte = !r.h;
            if (r.h) rasAssert(rex == 0x40, RAS_ERR_BAD_RH);
            if (useM && !m.isMem && 4 <= m.r.idx && m.r.idx <= 7) {
                rasAssert(r.h == m.r.h, RAS_ERR_BAD_RH);
            }
        } else if (useM && !m.isMem && 4 <= m.r.idx && m.r.idx <= 7) {
            uniformByte = !m.r.h;
            if (m.r.h) rasAssert(rex == 0x40, RAS_ERR_BAD_RH);
        }
    } else {
        rasAssert(!r.h && (!useM || m.isMem || !m.r.h), RAS_ERR_BAD_RH);
    }
    rasAssert(!useM || !m.isMem || (!m.m.base.h && !m.m.index.h),
              RAS_ERR_BAD_RH);
    if (sz == 1) BYTE(0x66);
    if (rex != 0x40 || uniformByte) BYTE(rex);
}

void rasEmitSizePrefixesSSE(rasBlock* ctx, int sz, bool w, rasX64Xmm r,
                          rasX64Op m, bool useM) {
    static const u8 szpfx[] = {0, 0x66, 0xf3, 0xf2};
    if (sz) BYTE(szpfx[sz]);
    rasEmitSizePrefixes(ctx, 2 | w, (rasX64Reg) {r.idx}, m, useM);
}

void rasEmitOpcode(rasBlock* ctx, u32 opcode) {
    if (opcode >> 16) BYTE(opcode >> 16);
    if (opcode >> 8) BYTE(opcode >> 8);
    BYTE(opcode);
}

void rasEmitMemOp(rasBlock* ctx, u8 r, rasX64Op m, int immBytes) {
    if (!m.isMem) {
        BYTE(MODRM(3, r & 7, m.r.idx & 7));
        return;
    }
    if (m.m.rip) {
        BYTE(MODRM(0, r & 7, RBP.idx));
        rasAddPatchEx(ctx, RAS_PATCH_REL32, m.m.lab, -(4 + immBytes));
        rasEmit32(ctx, 0);
        return;
    }
    rasAssert(m.m.disp == (s32) m.m.disp, RAS_ERR_BAD_IMM);
    u8 mod;
    if ((m.m.disp == 0 || m.m.nobase) && (m.m.base.idx & 7) != RBP.idx) mod = 0;
    else if (m.m.disp == (s8) m.m.disp) mod = 1;
    else mod = 2;
    bool needsib = m.m.nobase || !m.m.noindex || (m.m.base.idx & 7) == RSP.idx;
    BYTE(MODRM(mod, r & 7, needsib ? RSP.idx : m.m.base.idx & 7));
    if (needsib) {
        rasAssert((m.m.index.idx & 7) != RSP.idx, RAS_ERR_BAD_ADDR);
        u8 s = 0;
        if (!m.m.noindex) {
            switch (m.m.scale) {
                case 1: s = 0; break;
                case 2: s = 1; break;
                case 4: s = 2; break;
                case 8: s = 3; break;
                default: rasAssert(false, RAS_ERR_BAD_CONST);
            }
        }
        BYTE(SIB(s, m.m.noindex ? RSP.idx : m.m.index.idx & 7,
                 m.m.nobase ? RBP.idx : m.m.base.idx & 7));
    }
    if (mod == 1) {
        BYTE(m.m.disp);
    } else if (mod == 2 ||
               (mod == 0 && ((m.m.base.idx & 7) == RBP.idx || m.m.nobase))) {
        DWORD(m.m.disp);
    }
}
