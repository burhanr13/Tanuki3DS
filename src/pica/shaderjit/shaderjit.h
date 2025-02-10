#ifndef SHADERJIT_H
#define SHADERJIT_H

#include <common.h>
#include <pica/shader.h>

typedef struct _GPU GPU;

typedef void (*ShaderJitFunc)(ShaderUnit* shu);

typedef struct _ShaderJitBlock {
    u64 hash;
    void* backend;

    struct _ShaderJitBlock *next, *prev;
} ShaderJitBlock;

ShaderJitFunc shaderjit_get(GPU* gpu, ShaderUnit* shu);

#endif
