#ifndef __RAS_H
#define __RAS_H

#include <stddef.h>
#include <stdint.h>

#define bool _Bool
#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define s32 int32_t
#define u64 uint64_t

typedef struct _rasBlock rasBlock;

typedef struct _rasSymbol* rasLabel;

enum {
    RAS_FLAG_AUTOGROW = 1 << 0,
};

typedef enum {
    RAS_OK,
    RAS_ERR_CODE_SIZE,
    RAS_ERR_BAD_R31,
    RAS_ERR_BAD_RH,
    RAS_ERR_BAD_IMM,
    RAS_ERR_BAD_CONST,
    RAS_ERR_BAD_ADDR,
    RAS_ERR_UNDEF_LABEL,
    RAS_ERR_BAD_LABEL,

    RAS_ERR_MAX
} rasError;

typedef enum {
    RAS_PATCH_ABS64,

    RAS_PATCH_REL8,
    RAS_PATCH_REL32,

    RAS_PATCH_REL26,
    RAS_PATCH_REL19,
    RAS_PATCH_REL14,
    RAS_PATCH_REL21,
    RAS_PATCH_PGREL21,
    RAS_PATCH_PGOFF12,
} rasPatchType;

extern char* rasErrorStrings[];

typedef void (*rasErrorCallback)(rasError, void*);

void rasSetErrorCallback(rasErrorCallback cb, void* userdata);

rasBlock* rasCreate(size_t initialSize, int flags);
void rasDestroy(rasBlock* ctx);

void rasReady(rasBlock* ctx);
void* rasGetCode(rasBlock* ctx);
size_t rasGetSize(rasBlock* ctx);

void rasAssert(bool condition, rasError err);

rasLabel rasDeclareLabel(rasBlock* ctx);
rasLabel rasDefineLabel(rasBlock* ctx, rasLabel l);
rasLabel rasDefineLabelExternal(rasLabel l, void* addr);
void* rasGetLabelAddr(rasBlock* ctx, rasLabel l);

void rasAddPatchEx(rasBlock* ctx, rasPatchType type, rasLabel l, int addend);
static inline void rasAddPatch(rasBlock* ctx, rasPatchType type, rasLabel l) {
    rasAddPatchEx(ctx, type, l, 0);
}

void rasEmit8(rasBlock* ctx, u8 b);
void rasEmit16(rasBlock* ctx, u16 h);
void rasEmit32(rasBlock* ctx, u32 w);
void rasEmit64(rasBlock* ctx, u64 d);

void rasAlign(rasBlock* ctx, size_t alignment);

#undef bool
#undef u8
#undef u16
#undef u32
#undef s32
#undef u64

#endif
