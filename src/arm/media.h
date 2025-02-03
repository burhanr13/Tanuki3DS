#ifndef MEDIA_H
#define MEDIA_H

#include "arm_core.h"

u32 media_uadd8(ArmCore* cpu, u32 a, u32 b);
u32 media_usub8(ArmCore* cpu, u32 a, u32 b);
u32 media_uqadd8(ArmCore* cpu, u32 a, u32 b);
u32 media_uqsub8(ArmCore* cpu, u32 a, u32 b);
u32 media_uhadd8(ArmCore* cpu, u32 a, u32 b);
u32 media_ssub8(ArmCore* cpu, u32 a, u32 b);
u32 media_qsub8(ArmCore* cpu, u32 a, u32 b);

u32 media_sel(ArmCore* cpu, u32 a, u32 b);

#endif
