#ifndef SHADER_JIT_X86_H
#define SHADER_JIT_X86_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../shader.h"
#include "shaderjit.h"

void* shaderjit_x86_init();
ShaderJitFunc shaderjit_x86_get_code(void* backend, ShaderUnit* shu);
void shaderjit_x86_free(void* backend);
void shaderjit_x86_disassemble(void* backend);

#ifdef __cplusplus
}
#endif

#endif