#ifndef SHADER_JIT_ARM_H
#define SHADER_JIT_ARM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pica/shader.h"

#include "shaderjit.h"

void* shaderjit_arm_init();
ShaderJitFunc shaderjit_arm_get_code(void* backend, ShaderUnit* shu);
void shaderjit_arm_free(void* backend);
void shaderjit_arm_disassemble(void* backend);

#ifdef __cplusplus
}
#endif

#endif