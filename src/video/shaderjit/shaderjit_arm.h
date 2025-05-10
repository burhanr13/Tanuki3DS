#ifndef SHADER_JIT_ARM_H
#define SHADER_JIT_ARM_H

#include <ras/ras.h>

#include "common.h"
#include "video/shader.h"

#include "shaderjit.h"

typedef struct {
    u32 pc;
    rasLabel lab;
} ArmShaderEntrypoint;

typedef struct {
    rasBlock* code;

    Vector(rasLabel) jmplabels;
    Vector(PICAInstr) calls;
    Vector(ArmShaderEntrypoint) entrypoints;
    rasLabel ex2func, lg2func;
    bool usingex2, usinglg2;

} ArmShaderJitBackend;

ArmShaderJitBackend* shaderjit_arm_init();
ShaderJitFunc shaderjit_arm_get_code(ArmShaderJitBackend* backend,
                                     ShaderUnit* shu);
void shaderjit_arm_free(ArmShaderJitBackend* backend);
void shaderjit_arm_disassemble(ArmShaderJitBackend* backend);

#endif