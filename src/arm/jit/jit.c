#include "jit.h"

#include "backend/backend.h"
#include "optimizer.h"
#include "register_allocator.h"
#include "translator.h"

// #define JIT_DISASM
// #define JIT_CPULOG
// #define IR_INTERPRET
// #define NO_OPTS

#ifdef JIT_DISASM
#define IR_DISASM
#ifndef IR_INTERPRET
#define BACKEND_DISASM
#endif
#endif

JITBlock* create_jit_block(ArmCore* cpu, u32 addr) {
    JITBlock* block = malloc(sizeof *block);
    block->attrs = cpu->cpsr.w & 0x3f;
    block->start_addr = addr;

    Vec_init(block->linkingblocks);

    IRBlock ir;
    irblock_init(&ir);

    compile_block(cpu, &ir, addr);

    block->numinstr = ir.numinstr;

#ifndef NO_OPTS
    optimize_loadstore(&ir);
    optimize_constprop(&ir);
    optimize_literals(&ir, cpu);
    optimize_chainjumps(&ir);
    optimize_loadstore(&ir);
    optimize_constprop(&ir);
    optimize_chainjumps(&ir);
    optimize_deadcode(&ir);
    optimize_blocklinking(&ir, cpu);
#endif

    block->end_addr = ir.end_addr;

    RegAllocation regalloc = allocate_registers(&ir);

#ifdef IR_DISASM
    ir_disassemble(&ir);
    regalloc_print(&regalloc);
#endif

    block->backend = backend_generate_code(&ir, &regalloc, cpu);
    block->code = backend_get_code(block->backend);

    block->cpu = cpu;

    cpu->jit_cache[block->attrs][addr >> 16][(addr & 0xffff) >> 1] = block;
    backend_patch_links(block);

#ifdef BACKEND_DISASM
    backend_disassemble(block->backend);
#endif

    regalloc_free(&regalloc);
#ifdef IR_INTERPRET
    block->ir = malloc(sizeof(IRBlock));
    *block->ir = ir;
#else
    irblock_free(&ir);
#endif

    return block;
}

void destroy_jit_block(JITBlock* block) {
#ifdef IR_INTERPRET
    irblock_free(block->ir);
    free(block->ir);
#endif

    block->cpu->jit_cache[block->attrs][block->start_addr >> 16]
                         [(block->start_addr & 0xffff) >> 1] = NULL;
    backend_free(block->backend);
    Vec_foreach(l, block->linkingblocks) {
        if (!(block->cpu->jit_cache[l->attrs] &&
              block->cpu->jit_cache[l->attrs][l->addr >> 16]))
            continue;
        if (block->cpu->jit_cache[l->attrs] &&
            block->cpu->jit_cache[l->addr >> 16]) {
            JITBlock* linkingblock =
                block->cpu->jit_cache[l->attrs][l->addr >> 16]
                                     [(l->addr & 0xffff) >> 1];
            if (linkingblock) destroy_jit_block(linkingblock);
        }
    }
    Vec_free(block->linkingblocks);
    free(block);
}

void jit_exec(JITBlock* block) {
#ifdef JIT_CPULOG
    cpu_print_state(block->cpu);
#endif
#ifdef IR_INTERPRET
    ir_interpret(block->ir, block->cpu);
#else
    block->code();
#endif
}

// the jit cache is formatted as follows
// 64 root entries corresponding to low 6 bits of cpsr of the block
// then BIT(16) entries corresponding to bit[31:16] of start addr
// then BIT(15) entries corresponding to bit[15:1] of start addr
JITBlock* get_jitblock(ArmCore* cpu, u32 attrs, u32 addr) {
    u32 addrhi = addr >> 16;
    u32 addrlo = (addr & 0xffff) >> 1;

    if (!cpu->jit_cache[attrs]) {
        cpu->jit_cache[attrs] = calloc(BIT(16), sizeof(JITBlock**));
    }

    if (!cpu->jit_cache[attrs][addrhi]) {
        cpu->jit_cache[attrs][addrhi] = calloc(BIT(16) >> 1, sizeof(JITBlock*));
    }

    JITBlock* block = NULL;
    if (!cpu->jit_cache[attrs][addrhi][addrlo]) {
        u32 old = cpu->cpsr.jitattrs;
        cpu->cpsr.jitattrs = attrs;
        block = create_jit_block(cpu, addr);
        cpu->cpsr.jitattrs = old;
        cpu->jit_cache[attrs][addrhi][addrlo] = block;
    } else {
        block = cpu->jit_cache[attrs][addrhi][addrlo];
    }

    return block;
}

void jit_free_all(ArmCore* cpu) {
    for (int i = 0; i < 64; i++) {
        if (cpu->jit_cache[i]) {
            for (int j = 0; j < BIT(16); j++) {
                if (cpu->jit_cache[i][j]) {
                    for (int k = 0; k < BIT(16) >> 1; k++) {
                        if (cpu->jit_cache[i][j][k]) {
                            destroy_jit_block(cpu->jit_cache[i][j][k]);
                            cpu->jit_cache[i][j][k] = NULL;
                        }
                    }
                    free(cpu->jit_cache[i][j]);
                    cpu->jit_cache[i][j] = NULL;
                }
            }
            free(cpu->jit_cache[i]);
            cpu->jit_cache[i] = NULL;
        }
    }
}

void arm_exec_jit(ArmCore* cpu) {
    JITBlock* block = get_jitblock(cpu, cpu->cpsr.jitattrs, cpu->pc);
    jit_exec(block);
}
