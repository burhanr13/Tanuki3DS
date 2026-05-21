#include "ras.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sys/mman.h>
#ifndef MAP_JIT
#define MAP_JIT 0
#endif
#endif

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

typedef enum {
    SYM_UNDEFINED,
    SYM_INTERNAL,
    SYM_EXTERNAL,
} rasSymbolType;

typedef struct _rasSymbol {
    rasSymbolType type;
    union {
        size_t intOffset;
        void* extAddr;
    };
} rasSymbol;

typedef struct _rasPatch {
    rasPatchType type;
    size_t offset;
    rasLabel sym;
    int addend;
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

    u8* code;
    u8* curr;
    size_t size;

    size_t initialSize;
    int flags;

    LISTNODE(rasSymbol) symbols;
    LISTNODE(rasPatch) patches;

} rasBlock;

char* rasErrorStrings[RAS_ERR_MAX] = {
    [RAS_OK] = "no error",
    [RAS_ERR_CODE_SIZE] = "ran out of space for code",
    [RAS_ERR_BAD_R31] = "invalid use of ZR/SP",
    [RAS_ERR_BAD_RH] = "invalid use of high byte register",
    [RAS_ERR_BAD_IMM] = "immediate out of range",
    [RAS_ERR_BAD_CONST] = "invalid constant operand",
    [RAS_ERR_BAD_ADDR] = "invalid addressing mode",
    [RAS_ERR_UNDEF_LABEL] = "undefined label",
    [RAS_ERR_BAD_LABEL] = "label out of range or misaligned",
};

rasErrorCallback errorCallback = NULL;
void* errorUserdata = NULL;

static void* jit_alloc(size_t size) {
#ifdef _WIN32
    void* ptr = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE,
                             PAGE_EXECUTE_READWRITE);
    if (!ptr) {
        fprintf(stderr, "VirtualAlloc failed\n");
        abort();
    }
#else
    // try to map near the static code
    void* ptr = mmap(rasErrorStrings, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap");
        abort();
    }
#ifdef __APPLE__
    pthread_jit_write_protect_np(0);
#endif
#endif
    return ptr;
}

static void jit_free(void* code, size_t size) {
#ifdef _WIN32
    VirtualFree(code, 0, MEM_RELEASE);
#else
    munmap(code, size);
#endif
}

void rasSetErrorCallback(rasErrorCallback cb, void* userdata) {
    errorCallback = cb;
    errorUserdata = userdata;
}

rasBlock* rasCreate(size_t initialSize, int flags) {
    rasBlock* ctx = calloc(1, sizeof *ctx);

    ctx->code = jit_alloc(initialSize);
    ctx->curr = ctx->code;
    ctx->size = initialSize;

    ctx->initialSize = initialSize;

    ctx->symbols = NULL;
    ctx->patches = NULL;

    return ctx;
}

void rasDestroy(rasBlock* ctx) {
    jit_free(ctx->code, ctx->size);

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
        case SYM_INTERNAL: return ctx->code + l->intOffset;
        case SYM_EXTERNAL: return l->extAddr;
        case SYM_UNDEFINED:
        default: return NULL;
    }
}

void rasAddPatchEx(rasBlock* ctx, rasPatchType type, rasLabel l, int addend) {
    rasPatch* p = LISTNEXT(ctx->patches);
    p->type = type;
    p->offset = ctx->curr - ctx->code;
    p->sym = l;
    p->addend = addend;
}

void rasApplyPatch(rasBlock* ctx, rasPatch p) {
    void* patchaddr = ctx->code + p.offset;
    void* symaddr = rasGetLabelAddr(ctx, p.sym);
    rasAssert(symaddr != NULL, RAS_ERR_UNDEF_LABEL);

    ptrdiff_t reladdr = symaddr + p.addend - patchaddr;

    u32* patchinst = patchaddr;

    switch (p.type) {
        case RAS_PATCH_ABS64: {
            *(void**) patchaddr = symaddr;
            break;
        }
        case RAS_PATCH_REL8: {
            rasAssert(ISNBITSS64(reladdr, 8), RAS_ERR_BAD_LABEL);
            *(s8*) patchaddr = reladdr;
            break;
        }
        case RAS_PATCH_REL32: {
            rasAssert(ISNBITSS64(reladdr, 32), RAS_ERR_BAD_LABEL);
            *(s32*) patchaddr = reladdr;
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

#ifdef __APPLE__
    pthread_jit_write_protect_np(1);
#endif
#ifdef __aarch64__
    __builtin___clear_cache(ctx->code, ctx->code + ctx->size);
#endif
}

void* rasGetCode(rasBlock* ctx) {
    return ctx->code;
}

size_t rasGetSize(rasBlock* ctx) {
    return ctx->curr - ctx->code;
}

void rasAssert(bool condition, rasError err) {
    if (!condition) {
        if (errorCallback) {
            errorCallback(err, errorUserdata);
        } else {
            fprintf(stderr, "ras error: %s\n", rasErrorStrings[err]);
            abort();
        }
    }
}

static void ras_grow(rasBlock* ctx) {
    u8* oldCode = ctx->code;
    size_t oldSize = ctx->size;
    ctx->size *= 2;
    ctx->code = jit_alloc(ctx->size);
    ctx->curr = ctx->code + oldSize;
    memcpy(ctx->code, oldCode, oldSize);
    jit_free(oldCode, oldSize);
}

void rasEmit8(rasBlock* ctx, u8 b) {
    if (ctx->flags & RAS_FLAG_AUTOGROW) {
        if (ctx->curr == ctx->code + ctx->size) ras_grow(ctx);
    } else {
        rasAssert(ctx->curr != ctx->code + ctx->size, RAS_ERR_CODE_SIZE);
    }
    *ctx->curr++ = b;
}

void rasEmit16(rasBlock* ctx, u16 h) {
    rasEmit8(ctx, h);
    rasEmit8(ctx, h >> 8);
}

void rasEmit32(rasBlock* ctx, u32 w) {
    if (ctx->flags & RAS_FLAG_AUTOGROW) {
        if (ctx->curr + 4 > ctx->code + ctx->size) ras_grow(ctx);
    } else {
        rasAssert(ctx->curr + 4 <= ctx->code + ctx->size, RAS_ERR_CODE_SIZE);
    }
    *(u32*) ctx->curr = w;
    ctx->curr += 4;
}

void rasEmit64(rasBlock* ctx, u64 d) {
    rasEmit32(ctx, d);
    rasEmit32(ctx, d >> 32);
}

void rasAlign(rasBlock* ctx, size_t alignment) {
    for (int i = 0; i < 64; i++) {
        if (alignment & BIT(i)) {
            if (alignment != BIT(i)) return;
            break;
        }
    }
    size_t cur = ctx->curr - ctx->code;
    size_t aligned = (cur + (alignment - 1)) & ~(alignment - 1);
    ctx->curr += aligned - cur;
}
