#ifndef __RAS_IMPL_H
#define __RAS_IMPL_H

#include "ras.h"
#include "ras_macros.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>

typedef int8_t s8;
typedef uint8_t u8;
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;

#define BIT(b) (1ull << (b))
#define MASK(b) (BIT(b) - 1)
#define ISNBITSU64(n, b) ((u64) (n) >> (b) == 0)
#define ISNBITSS64(n, b)                                                       \
    ((s64) (n) >> ((b) - 1) == 0 || (s64) (n) >> ((b) - 1) == -1)
#define ISLOWBITS0(n, b) (((n) & MASK(b)) == 0)

typedef enum {
    SYM_UNDEFINED,
    SYM_INTERNAL,
    SYM_EXTERNAL,
} rasSymbolType;

typedef struct _rasSymbol {
    rasSymbolType type;
    union {
        size_t intOffset; // in words
        void* extAddr;
    };
} rasSymbol;

typedef struct _rasPatch {
    rasPatchType type;
    size_t offset; // in words
    rasLabel sym;
} rasPatch;

#define LISTNODELEN 64

#define LISTNODE(T)                                                            \
    struct ListNode_##T {                                                      \
        T d[LISTNODELEN];                                                      \
        size_t count;                                                          \
        struct ListNode_##T* next;                                             \
    }*

#define LISTNEXT(l)                                                            \
    ({                                                                         \
        if (!(l) || (l)->count == LISTNODELEN) {                               \
            typeof(l) n = malloc(sizeof *n);                                   \
            n->count = 0;                                                      \
            n->next = (l);                                                     \
            (l) = n;                                                           \
        }                                                                      \
        &(l)->d[(l)->count++];                                                 \
    })

#define LISTPOP(l)                                                             \
    ({                                                                         \
        typeof(l) tmp = (l)->next;                                             \
        free(l);                                                               \
        (l) = tmp;                                                             \
    })

typedef struct _rasBlock {

    u32* code;
    u32* curr;
    size_t size; // in words

    size_t initialSize;

    LISTNODE(rasSymbol) symbols;
    LISTNODE(rasPatch) patches;

} rasBlock;

char* rasErrorStrings[RAS_ERR_MAX] = {
    [RAS_OK] = "no error",
    [RAS_ERR_CODE_SIZE] = "ran out of space for code",
    [RAS_ERR_BAD_R31] = "invalid use of zr/sp",
    [RAS_ERR_BAD_IMM] = "immediate out of range",
    [RAS_ERR_BAD_CONST] = "invalid constant operand (shift, extend, etc)",
    [RAS_ERR_UNDEF_LABEL] = "undefined label",
    [RAS_ERR_BAD_LABEL] = "label out of range or misaligned",
};

rasErrorCallback errorCallback = NULL;
void* errorUserdata = NULL;

static void* jit_alloc(size_t size) {
#ifdef RAS_USE_RWX
    int prot = PROT_READ | PROT_WRITE | PROT_EXEC;
#else
    int prot = PROT_READ | PROT_WRITE;
#endif
    // try to map near the static code
    void* ptr =
        mmap(rasErrorStrings, size, prot, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap");
        abort();
    }
    return ptr;
}

enum Perm {
    RW,
    RX,
    RWX,
};

static void jit_protect(void* code, size_t size, enum Perm perm) {
    switch (perm) {
        case RW:
            mprotect(code, size, PROT_READ | PROT_WRITE);
            break;
        case RX:
            mprotect(code, size, PROT_READ | PROT_EXEC);
            break;
        case RWX:
            mprotect(code, size, PROT_READ | PROT_WRITE | PROT_EXEC);
            break;
    }
}

static void jit_free(void* code, size_t size) {
    munmap(code, size);
}

static void jit_clearcache(void* code, size_t size) {
    __builtin___clear_cache(code, code + size);
}

void rasSetErrorCallback(rasErrorCallback cb, void* userdata) {
    errorCallback = cb;
    errorUserdata = userdata;
}

rasBlock* rasCreate(size_t initialSize) {
    rasBlock* ctx = calloc(1, sizeof *ctx);

    ctx->code = jit_alloc(initialSize);
    ctx->curr = ctx->code;
    ctx->size = initialSize / 4;

    ctx->initialSize = initialSize;

    ctx->symbols = NULL;
    ctx->patches = NULL;

    return ctx;
}

void rasDestroy(rasBlock* ctx) {
    jit_free(ctx->code, 4 * ctx->size);

    free(ctx);
}

rasLabel rasDeclareLabel(rasBlock* ctx) {
    rasSymbol* l = LISTNEXT(ctx->symbols);
    l->type = SYM_UNDEFINED;
    return l;
}

rasLabel rasDefineLabel(rasBlock* ctx, rasLabel l) {
    l->type = SYM_INTERNAL;
    l->intOffset = ctx->curr - ctx->code;
    return l;
}

rasLabel rasDefineLabelExternal(rasLabel l, void* addr) {
    l->type = SYM_EXTERNAL;
    l->extAddr = addr;
    return l;
}

void* rasGetLabelAddr(rasBlock* ctx, rasLabel l) {
    switch (l->type) {
        case SYM_INTERNAL:
            return ctx->code + l->intOffset;
        case SYM_EXTERNAL:
            return l->extAddr;
        case SYM_UNDEFINED:
        default:
            return NULL;
    }
}

void rasAddPatch(rasBlock* ctx, rasPatchType type, rasLabel l) {
    rasPatch* p = LISTNEXT(ctx->patches);
    p->type = type;
    p->offset = ctx->curr - ctx->code;
    p->sym = l;
}

void rasApplyPatch(rasBlock* ctx, rasPatch p) {
    void* patchaddr = &ctx->code[p.offset];
    void* symaddr = rasGetLabelAddr(ctx, p.sym);
    rasAssert(symaddr != NULL, RAS_ERR_UNDEF_LABEL);

    ptrdiff_t reladdr = symaddr - patchaddr;

    u32* patchinst = patchaddr;

    switch (p.type) {
        case RAS_PATCH_ABS64: {
            *(void**) patchaddr = symaddr;
            break;
        }
        case RAS_PATCH_REL26: {
            rasAssert(ISLOWBITS0(reladdr, 2), RAS_ERR_BAD_LABEL);
            reladdr >>= 2;
            rasAssert(ISNBITSS64(reladdr, 26), RAS_ERR_BAD_LABEL);
            reladdr &= MASK(26);
            *patchinst |= reladdr;
            break;
        }
        case RAS_PATCH_PGREL21:
            reladdr = ((size_t) symaddr >> 12) - ((size_t) patchaddr >> 12);
            __attribute__((fallthrough));
        case RAS_PATCH_REL19:
        case RAS_PATCH_REL21: {
            if (p.type == RAS_PATCH_REL19) {
                rasAssert(ISLOWBITS0(reladdr, 2), RAS_ERR_BAD_LABEL);
            } else {
                *patchinst |= (reladdr & MASK(2)) << 29;
            }
            reladdr >>= 2;
            rasAssert(ISNBITSS64(reladdr, 19), RAS_ERR_BAD_LABEL);
            reladdr &= MASK(19);
            *patchinst |= reladdr << 5;
            break;
        }
        case RAS_PATCH_REL14: {
            rasAssert(ISLOWBITS0(reladdr, 2), RAS_ERR_BAD_LABEL);
            reladdr >>= 2;
            rasAssert(ISNBITSS64(reladdr, 14), RAS_ERR_BAD_LABEL);
            reladdr &= MASK(14);
            *patchinst |= reladdr << 5;
            break;
        }
        case RAS_PATCH_PGOFF12: {
            *patchinst |= ((uintptr_t) symaddr & MASK(12)) << 10;
            break;
        }
    }
}

void rasApplyAllPatches(rasBlock* ctx) {
    while (ctx->patches) {
        for (int i = 0; i < ctx->patches->count; i++) {
            rasApplyPatch(ctx, ctx->patches->d[i]);
        }
        LISTPOP(ctx->patches);
    }
}

void rasReady(rasBlock* ctx) {
    rasApplyAllPatches(ctx);

#ifndef RAS_USE_RWX
    jit_protect(ctx->code, 4 * ctx->size, RX);
#endif
    jit_clearcache(ctx->code, 4 * ctx->size);
}

void rasUnready(rasBlock* ctx) {
#ifndef RAS_USE_RWX
    jit_protect(ctx->code, 4 * ctx->size, RW);
#endif
}

void* rasGetCode(rasBlock* ctx) {
    return ctx->code;
}

size_t rasGetSize(rasBlock* ctx) {
    return (ctx->curr - ctx->code) * 4;
}

void rasAssert(bool condition, rasError err) {
#ifndef RAS_NO_CHECKS
    if (!condition) {
        if (errorCallback) {
            errorCallback(err, errorUserdata);
        } else {
            fprintf(stderr, "ras error: %s\n", rasErrorStrings[err]);
            abort();
        }
    }
#endif
}

#ifdef RAS_AUTOGROW
static void ras_grow(rasBlock* ctx) {
    u32* oldCode = ctx->code;
    size_t oldSize = ctx->size;
    ctx->size *= 2;
    ctx->code = jit_alloc(4 * ctx->size);
    ctx->curr = ctx->code + oldSize;
    memcpy(ctx->code, oldCode, 4 * oldSize);
    jit_free(oldCode, 4 * oldSize);
}
#endif

void rasEmitWord(rasBlock* ctx, u32 w) {
#ifdef RAS_AUTOGROW
    if (ctx->curr == ctx->code + ctx->size) ras_grow(ctx);
#else
    rasAssert(ctx->curr != ctx->code + ctx->size, RAS_ERR_CODE_SIZE);
#endif
    *ctx->curr++ = w;
}

void rasEmitDword(rasBlock* ctx, u64 d) {
    word(d);
    word(d >> 32);
}

void rasAlign(rasBlock* ctx, size_t alignment) {
    if (alignment & 3) return;
    alignment >>= 2;
    for (int i = 0; i < 64; i++) {
        if (alignment & BIT(i)) {
            if (alignment != BIT(i)) return;
            break;
        }
    }
    size_t cur = ctx->curr - ctx->code;
    size_t aligned = (cur + (alignment - 1)) & ~(alignment - 1);
    for (int i = 0; i < aligned - cur; i++) word(0);
}

bool rasGenerateLogicalImm(u64 imm, u32 sf, u32* immr, u32* imms, u32* n) {
    if (!imm || !~imm) return false;
    u32 sz = sf ? 64 : 32;

    if (!sf) imm &= MASK(32);

    // find the first one bit and rotation
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

void rasEmitPseudoAddSubImm(rasBlock* ctx, u32 sf, u32 op, u32 s, rasReg rd,
                            rasReg rn, u64 imm, rasReg rtmp) {
    if (!sf) imm = (s32) imm;
    if (ISNBITSU64(imm, 12)) {
        addsub(sf, op, s, rd, rn, imm);
    } else if (ISNBITSU64(imm, 24) && ISLOWBITS0(imm, 12)) {
        addsub(sf, op, s, rd, rn, imm >> 12, lsl(12));
    } else {
        imm = -imm;
        if (ISNBITSU64(imm, 12)) {
            addsub(sf, !op, s, rd, rn, imm);
        } else if (ISNBITSU64(imm, 24) && ISLOWBITS0(imm, 12)) {
            addsub(sf, !op, s, rd, rn, imm >> 12, lsl(12));
        } else {
            imm = -imm;
            if (sf) {
                movx(rtmp, imm);
            } else {
                movw(rtmp, imm);
            }
            addsub(sf, op, s, rd, rn, rtmp);
        }
    }
}

void rasEmitPseudoLogicalImm(rasBlock* ctx, u32 sf, u32 opc, rasReg rd,
                             rasReg rn, u64 imm, rasReg rtmp) {
    u32 immr, imms, n;
    if (rasGenerateLogicalImm(imm, sf, &immr, &imms, &n)) {
        logical(sf, opc, 0, rd, rn, imm);
    } else {
        if (sf) {
            movx(rtmp, imm);
        } else {
            movw(rtmp, imm);
        }
        logical(sf, opc, 0, rd, rn, rtmp);
    }
}

void rasEmitPseudoMovImm(rasBlock* ctx, u32 sf, rasReg rd, u64 imm) {
    if (imm == 0) {
        if (sf) {
            movzx(rd, 0);
        } else {
            movzw(rd, 0);
        }
        return;
    } else if (imm == ~0u && !sf) {
        movnw(rd, 0);
        return;
    } else if (imm == ~0ull) {
        if (sf) {
            movnx(rd, 0);
        } else {
            movnw(rd, 0);
        }
        return;
    }

    u32 immr, imms, n;
    if (rasGenerateLogicalImm(imm, sf, &immr, &imms, &n)) {
        if (sf) {
            orrx(rd, zr, imm);
        } else {
            orrw(rd, zr, imm);
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
            movewide(sf, opc, rd, hw, lsl(16 * i));
        }
    }
}

void rasEmitPseudoMovReg(rasBlock* ctx, u32 sf, rasReg rd, rasReg rm) {
    if (rd.isSp || rm.isSp) {
        if (sf) {
            addx(rd, rm, 0);
        } else {
            addw(rd, rm, 0);
        }
    } else {
        if (sf) {
            orrx(rd, zr, rm);
        } else {
            orrw(rd, zr, rm);
        }
    }
}

void rasEmitPseudoShiftImm(rasBlock* ctx, u32 sf, u32 type, rasReg rd,
                           rasReg rn, u32 imm) {
    if (sf) {
        switch (type) {
            case 0:
                ubfizx(rd, rn, imm, 64 - imm);
                break;
            case 1:
                ubfxx(rd, rn, imm, 64 - imm);
                break;
            case 2:
                sbfxx(rd, rn, imm, 64 - imm);
                break;
            case 3:
                extrx(rd, rn, rn, imm);
                break;
        }
    } else {
        switch (type) {
            case 0:
                ubfizw(rd, rn, imm, 32 - imm);
                break;
            case 1:
                ubfxw(rd, rn, imm, 32 - imm);
                break;
            case 2:
                sbfxw(rd, rn, imm, 32 - imm);
                break;
            case 3:
                extrw(rd, rn, rn, imm);
                break;
        }
    }
}

void rasEmitPseudoPCRelAddrLong(rasBlock* ctx, rasReg rd, rasLabel lab) {
    adrp(rd, lab);
    rasAddPatch(ctx, RAS_PATCH_PGOFF12, lab);
    addx(rd, rd, 0);
}

#endif
