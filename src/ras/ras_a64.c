#include "ras_a64.h"

#include <stdbool.h>

typedef int8_t s8;
typedef uint8_t u8;
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;

#define BIT(B) (1ull << (B))
#define MASK(B) (BIT(B) - 1)
#define ISNBITSU64(n, B) ((u64) (n) >> (B) == 0)
#define ISNBITSS64(n, B)                                                       \
    ((s64) (n) >> ((B) - 1) == 0 || (s64) (n) >> ((B) - 1) == -1)
#define ISLOWBITS0(n, B) (((n) & MASK(B)) == 0)

bool rasGenerateLogicalImm(u64 imm, u32 sf, u32* immr, u32* imms, u32* n) {
    if (!imm || !~imm) return false;
    u32 sz = sf ? 64 : 32;

    if (!sf) imm &= MASK(32);

    // find the first one bit AND rotation
    u32 rot = 0;
    for (rot = 0; rot < sz; rot++) {
        if ((imm & BIT(rot)) && !(imm & BIT((rot - 1) & (sz - 1)))) {
            if (rot) imm = (imm >> rot) | (imm << (sz - rot));
            break;
        }
    }

    // find the pattern size of ones followed by zeros
    u32 ones = 0;
    for (int i = 0; i < sz; i++) {
        if (!(imm & BIT(i))) break;
        ones++;
    }
    if (ones == sz) return false;
    u32 zeros = 0;
    for (int i = ones; i < sz; i++) {
        if ((imm & BIT(i))) break;
        zeros++;
    }

    // check pattern size is power of 2
    u32 ptnsz = ones + zeros;
    u32 ptnszbits = 0;
    for (int i = 0; i < 6; i++) {
        if ((ptnsz & BIT(i))) break;
        ptnszbits++;
    }
    if (ptnsz != BIT(ptnszbits)) return false;

    // correct rotation to right rotation
    *immr = ptnsz - *immr;

    // verify pattern is correct
    for (int i = 0; i < sz >> ptnszbits; i++) {
        if ((imm & MASK(ptnsz)) != ((imm >> i * ptnsz) & MASK(ptnsz)))
            return false;
    }

    // calculate final result
    if (ptnszbits == 6) {
        *imms = ones - 1;
        *n = 1;
    } else {
        *imms = MASK(6) - MASK(ptnszbits + 1) + ones - 1;
        *n = 0;
    }
    if (rot) {
        rot &= MASK(ptnszbits);
        rot = ptnsz - rot;
    }
    *immr = rot;

    return true;
}

bool rasGenerateFPImm(float fimm, u8* imm8) {
    u32 imm = ((union {
                  float f;
                  u32 u;
              }) {fimm})
                  .u;
    u32 sgn = imm >> 31;
    u32 exp = (imm >> 23) & 0xff;
    u32 mant = imm & MASK(23);
    if (!ISLOWBITS0(mant, 19)) return 0;
    mant >>= 19;
    if (!(exp >> 2 == 0x1f || exp >> 2 == 0x20)) return 0;
    exp &= 7;
    *imm8 = sgn << 7 | exp << 4 | mant;
    return 1;
}

void rasEmitPseudoAddSubImm(rasBlock* ctx, u32 sf, u32 op, u32 s, rasA64Reg rd,
                            rasA64Reg rn, u64 imm, rasA64Reg rtmp) {
    if (!sf) imm = (s32) imm;
    if (ISNBITSU64(imm, 12)) {
        ADDSUB(sf, op, s, rd, rn, imm);
    } else if (ISNBITSU64(imm, 24) && ISLOWBITS0(imm, 12)) {
        ADDSUB(sf, op, s, rd, rn, imm >> 12, LSL(12));
    } else {
        imm = -imm;
        if (ISNBITSU64(imm, 12)) {
            ADDSUB(sf, !op, s, rd, rn, imm);
        } else if (ISNBITSU64(imm, 24) && ISLOWBITS0(imm, 12)) {
            ADDSUB(sf, !op, s, rd, rn, imm >> 12, LSL(12));
        } else {
            imm = -imm;
            if (sf) {
                MOVX(rtmp, imm);
            } else {
                MOVW(rtmp, imm);
            }
            ADDSUB(sf, op, s, rd, rn, rtmp);
        }
    }
}

void rasEmitPseudoLogicalImm(rasBlock* ctx, u32 sf, u32 opc, rasA64Reg rd,
                             rasA64Reg rn, u64 imm, rasA64Reg rtmp) {
    u32 immr, imms, n;
    if (rasGenerateLogicalImm(imm, sf, &immr, &imms, &n)) {
        LOGICAL(sf, opc, 0, rd, rn, imm);
    } else {
        if (sf) {
            MOVX(rtmp, imm);
        } else {
            MOVW(rtmp, imm);
        }
        LOGICAL(sf, opc, 0, rd, rn, rtmp);
    }
}

void rasEmitPseudoMovImm(rasBlock* ctx, u32 sf, rasA64Reg rd, u64 imm) {
    if (imm == 0) {
        if (sf) {
            MOVZX(rd, 0);
        } else {
            MOVZW(rd, 0);
        }
        return;
    } else if (imm == ~0u && !sf) {
        MOVNW(rd, 0);
        return;
    } else if (imm == ~0ull) {
        if (sf) {
            MOVNX(rd, 0);
        } else {
            MOVNW(rd, 0);
        }
        return;
    }

    u32 immr, imms, n;
    if (rasGenerateLogicalImm(imm, sf, &immr, &imms, &n)) {
        if (sf) {
            ORRX(rd, ZR, imm);
        } else {
            ORRW(rd, ZR, imm);
        }
        return;
    }

    int hw0s = 0;
    int hw1s = 0;

    int sz = sf ? 4 : 2;

    for (int i = 0; i < sz; i++) {
        u16 hw = imm >> 16 * i;
        if (hw == 0) hw0s++;
        if (hw == MASK(16)) hw1s++;
    }

    bool neg = hw1s > hw0s;
    bool initial = true;

    for (int i = 0; i < sz; i++) {
        u16 hw = imm >> 16 * i;
        if (hw != (neg ? MASK(16) : 0)) {
            u32 opc;
            if (initial) {
                initial = false;
                if (neg) {
                    opc = 0;
                    hw ^= MASK(16);
                } else {
                    opc = 2;
                }
            } else {
                opc = 3;
            }
            MOVEWIDE(sf, opc, rd, hw, LSL(16 * i));
        }
    }
}

void rasEmitPseudoMovReg(rasBlock* ctx, u32 sf, rasA64Reg rd, rasA64Reg rm) {
    if (rd.isSp || rm.isSp) {
        if (sf) {
            ADDX(rd, rm, 0);
        } else {
            ADDW(rd, rm, 0);
        }
    } else {
        if (sf) {
            ORRX(rd, ZR, rm);
        } else {
            ORRW(rd, ZR, rm);
        }
    }
}

void rasEmitPseudoShiftImm(rasBlock* ctx, u32 sf, u32 type, rasA64Reg rd,
                           rasA64Reg rn, u32 imm) {
    if (sf) {
        switch (type) {
            case 0:
                UBFIZX(rd, rn, imm, 64 - imm);
                break;
            case 1:
                UBFXX(rd, rn, imm, 64 - imm);
                break;
            case 2:
                SBFXX(rd, rn, imm, 64 - imm);
                break;
            case 3:
                EXTRX(rd, rn, rn, imm);
                break;
        }
    } else {
        switch (type) {
            case 0:
                UBFIZW(rd, rn, imm, 32 - imm);
                break;
            case 1:
                UBFXW(rd, rn, imm, 32 - imm);
                break;
            case 2:
                SBFXW(rd, rn, imm, 32 - imm);
                break;
            case 3:
                EXTRW(rd, rn, rn, imm);
                break;
        }
    }
}

void rasEmitPseudoPCRelAddrLong(rasBlock* ctx, rasA64Reg rd, rasLabel lab) {
    ADRP(rd, lab);
    rasAddPatch(ctx, RAS_PATCH_PGOFF12, lab);
    ADDX(rd, rd, 0);
}
