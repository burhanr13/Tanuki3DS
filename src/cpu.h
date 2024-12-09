#ifndef CPU_H
#define CPU_H

#include "3ds.h"
#include "arm/arm_core.h"
#include "common.h"

void cpu_init(E3DS* s);
void cpu_free(E3DS* s);

bool cpu_run(E3DS* s, int cycles);

u32 cpu_read8(E3DS* s, u32 addr, bool sx);
u32 cpu_read16(E3DS* s, u32 addr, bool sx);
u32 cpu_read32(E3DS* s, u32 addr);

void cpu_write8(E3DS* s, u32 addr, u8 b);
void cpu_write16(E3DS* s, u32 addr, u16 h);
void cpu_write32(E3DS* s, u32 addr, u32 w);

u16 cpu_fetch16(E3DS* s, u32 addr);
u32 cpu_fetch32(E3DS* s, u32 addr);

float cpu_readf32(E3DS* s, u32 addr);
double cpu_readf64(E3DS* s, u32 addr);
void cpu_writef32(E3DS* s, u32 addr, float f);
void cpu_writef64(E3DS* s, u32 addr, double d);

void cpu_handle_svc(E3DS* s, u32 num);

u32 cp15_read(E3DS* s, ArmInstr instr);
void cp15_write(E3DS* s, ArmInstr instr, u32 data);

#endif
