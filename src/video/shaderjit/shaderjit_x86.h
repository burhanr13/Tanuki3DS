#ifndef SHADER_JIT_X86_H
#define SHADER_JIT_X86_H

#include <ras/ras.h>

#include "video/shader.h"

#include "shaderjit.h"

typedef struct {
    u32 pc;
    rasLabel lab;
} X86ShaderEntrypoint;

typedef struct {
    rasBlock* code;

    Vec(rasLabel) jmplabels;
    Vec(PICAInstr) calls;
    Vec(X86ShaderEntrypoint) entrypoints;
    rasLabel ex2func, lg2func;
    bool usingEx2, usingLg2;
    rasLabel curEndLab;

} X86ShaderJitBackend;

X86ShaderJitBackend* shaderjit_x86_init();
ShaderJitFunc shaderjit_x86_get_code(X86ShaderJitBackend* backend, ShaderUnit* shu);
void shaderjit_x86_free(X86ShaderJitBackend* backend);
void shaderjit_x86_disassemble(X86ShaderJitBackend* backend);

#endif