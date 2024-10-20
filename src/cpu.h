#ifndef CPU_H
#define CPU_H

#include "3ds.h"
#include "arm/arm_core.h"
#include "common.h"

void cpu_init(HLE3DS* s);
void cpu_free(HLE3DS* s);

bool cpu_run(HLE3DS* s, int cycles);

u32 cpu_read8(HLE3DS* s, u32 addr, bool sx);
u32 cpu_read16(HLE3DS* s, u32 addr, bool sx);
u32 cpu_read32(HLE3DS* s, u32 addr);

void cpu_write8(HLE3DS* s, u32 addr, u8 b);
void cpu_write16(HLE3DS* s, u32 addr, u16 h);
void cpu_write32(HLE3DS* s, u32 addr, u32 w);

u16 cpu_fetch16(HLE3DS* s, u32 addr);
u32 cpu_fetch32(HLE3DS* s, u32 addr);

float cpu_readf32(HLE3DS* s, u32 addr);
double cpu_readf64(HLE3DS* s, u32 addr);
void cpu_writef32(HLE3DS* s, u32 addr, float f);
void cpu_writef64(HLE3DS* s, u32 addr, double d);

void cpu_handle_svc(HLE3DS* s, u32 num);

u32 cp15_read(HLE3DS* s, u32 cn, u32 cm, u32 cp);
void cp15_write(HLE3DS* s, u32 cn, u32 cm, u32 cp, u32 data);

#endif
