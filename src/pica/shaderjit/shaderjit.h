#ifndef SHADERJIT_H
#define SHADERJIT_H

#include "../../common.h"
#include "../shader.h"

typedef void (*ShaderJitFunc)(ShaderUnit* shu);

typedef struct {
    void* backend;
    ShaderJitFunc code;
} ShaderJitBlock;

void shaderjit_compile(ShaderJitBlock* block, ShaderUnit* shu);
void shaderjit_free(ShaderJitBlock* block);

#endif
