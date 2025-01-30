#ifndef REGISTER_ALLOCATOR_H
#define REGISTER_ALLOCATOR_H

#include "../../common.h"
#include "ir.h"

typedef enum {
    REG_NONE,
    REG_TEMP,
    REG_SAVED,
    REG_STACK,

    REG_MAX
} RegType;

typedef struct {
    u32 uses;
    RegType type;
} RegInfo;

typedef struct {
    u32 index;
    RegType type;
} HostRegInfo;

// reg_info is the vector of virtual registers
// reg_assn is the assignment of SSA vars to the regs in reg_info
// vars without an assignment have index -1
// nassns is the length of reg_assn
typedef struct {
    Vector(RegInfo) reg_info;
    u32* reg_assn;
    u32 nassns;
} RegAllocation;

// hostreg_info is the array of host regs corresponding to each virtual reg_info
// count is the number of registers of each type
// nregs is the length of hostreg_info
typedef struct {
    HostRegInfo* hostreg_info;
    u32 count[REG_MAX];
    u32 nregs;
} HostRegAllocation;

RegAllocation allocate_registers(IRBlock* block);
void regalloc_free(RegAllocation* regalloc);

HostRegAllocation allocate_host_registers(RegAllocation* regalloc, u32 ntemp,
                                          u32 nsaved);
void hostregalloc_free(HostRegAllocation* hostregs);

void regalloc_print(RegAllocation* regalloc);

#endif
