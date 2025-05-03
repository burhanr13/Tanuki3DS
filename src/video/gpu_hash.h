#ifndef GPU_HASH_H
#define GPU_HASH_H

#define XXH_INLINE_ALL
#include <xxh3.h>

#include "emulator.h"

#include "gpu.h"
#include "shader.h"

static inline u64 gpu_hash_texture(void* tex, u32 size) {
    if (!ctremu.hashTextures) return 0;
    // maybe we might want to do something different later but rn this is fine
    return XXH3_64bits(tex, size);
}

static inline u64 gpu_hash_sw_shader(ShaderUnit* shu) {
    return XXH3_64bits(shu->code, SHADER_CODE_SIZE * sizeof(PICAInstr));
}

static inline u64 gpu_hash_hw_shader(GPU* gpu) {
    // we need to hash the shader code, entrypoint, outmap_mask, and outmap
    XXH3_state_t* xxst = XXH3_createState();
    XXH3_64bits_reset(xxst);
    XXH3_64bits_update(xxst, gpu->vsh.progdata, sizeof gpu->vsh.progdata);
    XXH3_64bits_update(xxst, &gpu->regs.vsh.entrypoint,
                       sizeof gpu->regs.vsh.entrypoint);
    XXH3_64bits_update(xxst, &gpu->regs.vsh.outmap_mask,
                       sizeof gpu->regs.vsh.outmap_mask);
    XXH3_64bits_update(xxst, gpu->regs.raster.sh_outmap,
                       sizeof gpu->regs.raster.sh_outmap);
    u64 hash = XXH3_64bits_digest(xxst);
    XXH3_freeState(xxst);
    return hash;
}

static inline u64 gpu_hash_fs(UberUniforms* ubuf) {
    return XXH3_64bits(ubuf, sizeof *ubuf);
}

#endif