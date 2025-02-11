#ifndef SHADERJIT_H
#define SHADERJIT_H

#include <common.h>
#include <pica/shader.h>

typedef struct _GPU GPU;

typedef void (*ShaderJitFunc)(ShaderUnit* shu);

typedef struct _ShaderJitBlock {
    union {
        u64 hash;
        u64 key;
    };
    void* backend;

    struct _ShaderJitBlock *next, *prev;
} ShaderJitBlock;

ShaderJitFunc shaderjit_get(GPU* gpu, ShaderUnit* shu);
void shaderjit_free_all(GPU* gpu);

#endif
