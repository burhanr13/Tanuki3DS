#include "shaderjit.h"

#include "shaderjit_backend.h"

#define JIT_DISASM

void shaderjit_compile(ShaderJitBlock* block, ShaderUnit* shu) {
    block->backend = shaderjit_backend_compile(shu);
    block->code = shaderjit_backend_get_code(block->backend);
#ifdef JIT_DISASM
    pica_shader_disasm(shu);
    shaderjit_backend_disassemble(block->backend);
#endif
}

void shaderjit_free(ShaderJitBlock* block) {
    shaderjit_backend_free(block->backend);
    block->backend = NULL;
    block->code = NULL;
}