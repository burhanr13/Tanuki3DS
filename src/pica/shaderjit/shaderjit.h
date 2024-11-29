#ifndef SHADERJIT_H
#define SHADERJIT_H

#include "../../common.h"
#include "../shader.h"

#define VSH_MAX 8

typedef struct _GPU GPU;

typedef void (*ShaderJitFunc)(ShaderUnit* shu);

typedef struct _ShaderJitBlock {
    u64 hash;
    void* backend;

    struct _ShaderJitBlock* next;
    struct _ShaderJitBlock* prev;
} ShaderJitBlock;

typedef LRUCache(ShaderJitBlock, VSH_MAX) ShaderCache;

ShaderJitFunc shaderjit_get(GPU* gpu, ShaderUnit* shu);

#endif
