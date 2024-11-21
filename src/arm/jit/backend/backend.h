#ifndef BACKEND_H
#define BACKEND_H

#ifdef __x86_64__
#include "backend_x86.h"
#define backend_generate_code(ir, regalloc, cpu)                               \
    backend_x86_generate_code(ir, regalloc, cpu)
#define backend_get_code(backend) backend_x86_get_code(backend)
#define backend_patch_links(block) backend_x86_patch_links(block)
#define backend_free(backend) backend_x86_free(backend)
#define backend_disassemble(backend) backend_x86_disassemble(backend)
#else
#error("JIT not supported")
#endif

#endif