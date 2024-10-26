#include "jit.h"

#include "backend/backend.h"
#include "optimizer.h"
#include "translator.h"
#include "register_allocator.h"

//#define JIT_DISASM
//#define JIT_CPULOG
//#define IR_INTERPRET
//#define NO_OPTS

#ifdef JIT_DISASM
#define IR_DISASM
#ifndef IR_INTERPRET
#define BACKEND_DISASM
#endif
#endif

JITBlock* create_jit_block(ArmCore* cpu, u32 addr) {
    JITBlock* block = malloc(sizeof *block);
    block->start_addr = addr;

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

    block->backend = generate_code(&ir, &regalloc, cpu);
    block->code = get_code(block->backend);

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

    block->cpu = cpu;

    return block;
}

void destroy_jit_block(JITBlock* block) {
#ifdef IR_INTERPRET
    irblock_free(block->ir);
    free(block->ir);
#endif
    free_code(block->backend);
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

void jit_mark_dirty(ArmCore* cpu, u32 addr) {
    u32 addrhi = addr >> 16;
    u32 addrlo = addr & 0xffff;
    u32 byte = addrlo >> 9;
    u32 bit = (addrlo >> 6) & 7;
    if (cpu->jit_dirty && cpu->jit_dirty[addrhi]) {
        cpu->jit_dirty[addrhi][byte] |= BIT(bit);
    }
}

void clean_byte(u8* b, u32 stbit, u32 endbit) {
    u32 mask = (BIT(endbit + 1) - BIT(stbit));
    *b &= ~mask;
}

void invalidate_l2(ArmCore* cpu, u32 attr, u32 addrhi, u32 startlo, u32 endlo) {
    if (!cpu->jit_cache[attr][addrhi]) return;
    for (int i = startlo; i < endlo; i += 2) {
        if (cpu->jit_cache[attr][addrhi][i >> 1]) {
            destroy_jit_block(cpu->jit_cache[attr][addrhi][i >> 1]);
            cpu->jit_cache[attr][addrhi][i >> 1] = NULL;
        }
    }
    if (startlo == 0 && endlo == BIT(16)) {
        free(cpu->jit_cache[attr][addrhi]);
        cpu->jit_cache[attr][addrhi] = NULL;
    }
    if (cpu->jit_dirty[addrhi]) {
        endlo -= 1;
        u32 startbyte = startlo >> 9;
        u32 startbit = (startlo >> 6) & 7;
        u32 endbyte = endlo >> 9;
        u32 endbit = (endlo >> 6) & 7;
        if (startbyte == endbyte) {
            clean_byte(&cpu->jit_dirty[addrhi][startbyte], startbit, endbit);
        } else {
            clean_byte(&cpu->jit_dirty[addrhi][startbyte], startbit, 7);
            for (int i = startbyte + 1; i < endbyte; i++) {
                cpu->jit_dirty[addrhi][i] = 0;
            }
            clean_byte(&cpu->jit_dirty[addrhi][endbyte], 0, endbit);
        }
    }
}

void jit_invalidate_range(ArmCore* cpu, u32 start_addr, u32 end_addr) {
    if (start_addr >= end_addr) return;
    start_addr &= ~0x3f;
    end_addr = ((end_addr + 0x3f) & ~0x3f) - 1;
    u32 sthi = start_addr >> 16;
    u32 stlo = start_addr & 0xffff;
    u32 endhi = end_addr >> 16;
    u32 endlo = end_addr & 0xffff;

    for (int i = 0; i < 64; i++) {
        if (!cpu->jit_cache[i]) continue;
        if (sthi == endhi) {
            invalidate_l2(cpu, i, sthi, stlo, endlo + 1);
        } else {
            invalidate_l2(cpu, i, sthi, stlo, BIT(16));
            for (int j = sthi + 1; j < endhi; j++) {
                invalidate_l2(cpu, i, j, 0, BIT(16));
            }
            invalidate_l2(cpu, i, endhi, 0, endlo + 1);
        }
    }
}

bool is_byte_dirty(u8 b, u32 stbit, u32 endbit) {
    u32 mask = (BIT(endbit + 1) - BIT(stbit));
    return (b & mask) != 0;
}

bool jit_isdirty(ArmCore* cpu, u32 start, u32 end) {
    if (start >= end) return false;
    end -= 1;
    u32 sthi = start >> 16;
    u32 stbyte = (start >> 9) & MASK(7);
    u32 stbit = (start >> 6) & 7;
    u32 endhi = end >> 16;
    u32 endbyte = (end >> 9) & MASK(7);
    u32 endbit = (end >> 6) & 7;

    bool wasdirty = 0;
    if (sthi == endhi) {
        if (!cpu->jit_dirty[sthi]) return false;
        if (stbyte == endbyte) {
            return is_byte_dirty(cpu->jit_dirty[sthi][stbyte], stbit, endbit);
        } else {
            wasdirty |= is_byte_dirty(cpu->jit_dirty[sthi][stbyte], stbit, 7);
            for (int i = stbyte + 1; i < endbyte; i++) {
                wasdirty |= cpu->jit_dirty[sthi][i] != 0;
            }
            wasdirty |= is_byte_dirty(cpu->jit_dirty[sthi][endbyte], 0, endbit);
        }
    } else {
        wasdirty |= jit_isdirty(cpu, start, ((start + 0xffff) & ~0xffff));
        for (u32 a = ((start + 0xffff) & ~0xffff); a < (end & ~0xffff);
             a += BIT(16)) {
            wasdirty |= jit_isdirty(cpu, a, a + BIT(16));
        }
        wasdirty |= jit_isdirty(cpu, end & ~0xffff, end + 1);
    }
    return wasdirty;
}

JITBlock* get_jitblock(ArmCore* cpu, u32 attrs, u32 addr) {
    u32 addrhi = addr >> 16;
    u32 addrlo = (addr & 0xffff) >> 1;

    if (!cpu->jit_cache[attrs]) {
        cpu->jit_cache[attrs] = calloc(BIT(16), sizeof(JITBlock**));
        if (!cpu->jit_dirty) cpu->jit_dirty = calloc(BIT(16), sizeof(u8*));
    }

    if (!cpu->jit_cache[attrs][addrhi]) {
        cpu->jit_cache[attrs][addrhi] = calloc(BIT(16) >> 1, sizeof(JITBlock*));
        if (!cpu->jit_dirty[addrhi])
            cpu->jit_dirty[addrhi] = calloc(BIT(16) >> 6 >> 3, sizeof(u8));
    }

    JITBlock* block = NULL;
    if (!cpu->jit_cache[attrs][addrhi][addrlo]) {
        block = create_jit_block(cpu, addr);
        if (block->end_addr >> 16 != addrhi &&
            !cpu->jit_dirty[(addrhi + 1) & 0xffff]) {
            cpu->jit_dirty[(addrhi + 1) & 0xffff] =
                calloc(BIT(16) >> 6 >> 3, sizeof(u8));
        }
        if (jit_isdirty(cpu, block->start_addr, block->end_addr)) {
            jit_invalidate_range(cpu, block->start_addr, block->end_addr);
        }
        cpu->jit_cache[attrs][addrhi][addrlo] = block;
    } else {
        block = cpu->jit_cache[attrs][addrhi][addrlo];
        if (jit_isdirty(cpu, block->start_addr, block->end_addr)) {
            block = create_jit_block(cpu, addr);
            if (block->end_addr >> 16 != addrhi &&
                !cpu->jit_dirty[(addrhi + 1) & 0xffff]) {
                cpu->jit_dirty[(addrhi + 1) & 0xffff] =
                    calloc(BIT(16) >> 6 >> 3, sizeof(u8));
            }
            jit_invalidate_range(cpu, block->start_addr, block->end_addr);
            cpu->jit_cache[attrs][addrhi][addrlo] = block;
        }
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
                        }
                    }
                    free(cpu->jit_cache[i][j]);
                }
            }
            free(cpu->jit_cache[i]);
            cpu->jit_cache[i] = NULL;
        }
        if (cpu->jit_dirty) {
            for (int j = 0; j < BIT(16); j++) {
                free(cpu->jit_dirty[j]);
            }
            free(cpu->jit_dirty);
            cpu->jit_dirty = NULL;
        }
    }
}

void arm_exec_jit(ArmCore* cpu) {
    JITBlock* block = get_jitblock(cpu, cpu->cpsr.w & 0x3f, cpu->pc);
    jit_exec(block);
}
