#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "../../common.h"
#include "ir.h"

void optimize_loadstore(IRBlock* block);
void optimize_constprop(IRBlock* block);
void optimize_literals(IRBlock* block, ArmCore* cpu);
void optimize_chainjumps(IRBlock* block);
void optimize_deadcode(IRBlock* block);
void optimize_waitloop(IRBlock* block);
void optimize_blocklinking(IRBlock* block, ArmCore* cpu);

#endif
