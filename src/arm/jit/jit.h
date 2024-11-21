#ifndef JIT_BLOCK_H
#define JIT_BLOCK_H

#include "../arm_core.h"
#include "ir.h"

#define MAX_BLOCK_INSTRS 128
#define MAX_BLOCK_SIZE (MAX_BLOCK_INSTRS * 4)

typedef void (*JITFunc)();

typedef struct {
    u32 attrs;
    u32 addr;
} BlockLocation;

typedef struct _JITBlock {
    JITFunc code;
    void* backend;

    u32 attrs;
    u32 start_addr;
    u32 end_addr;
    u32 numinstr;

    ArmCore* cpu;
    IRBlock* ir;

    Vector(BlockLocation) linkingblocks;

} JITBlock;

JITBlock* create_jit_block(ArmCore* cpu, u32 addr);
void destroy_jit_block(JITBlock* block);

void jit_exec(JITBlock* block);
JITBlock* get_jitblock(ArmCore* cpu, u32 attrs, u32 addr);

void jit_invalidate_range(ArmCore* cpu, u32 start_addr, u32 end_addr);
void jit_free_all(ArmCore* cpu);

void arm_exec_jit(ArmCore* cpu);

#endif
