#ifndef SHADERJIT_BACKEND_H
#define SHADERJIT_BACKEND_H

#ifdef __x86_64__
#include "shaderjit_x86.h"
#define shaderjit_backend_init() shaderjit_x86_init()
// gets the code for the current entrypoint of this shader (set in shu)
#define shaderjit_backend_get_code(backend, shu) shaderjit_x86_get_code(backend, shu)
#define shaderjit_backend_free(backend) shaderjit_x86_free(backend)
#define shaderjit_backend_disassemble(backend) shaderjit_x86_disassemble(backend)
#else
#error("jit not supported")
#endif

#endif