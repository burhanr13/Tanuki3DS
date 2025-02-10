#include "shaderjit.h"

#define XXH_INLINE_ALL
#include <xxh3.h>

#include <pica/gpu.h>

#include "shaderjit_backend.h"

ShaderJitFunc shaderjit_get(GPU* gpu, ShaderUnit* shu) {
    u64 hash = XXH3_64bits(shu->code, SHADER_CODE_SIZE * sizeof(PICAInstr));
    ShaderJitBlock* block = nullptr;
    for (int i = 0; i < VSH_MAX; i++) {
        if (gpu->vshaders_sw.d[i].hash == hash ||
            gpu->vshaders_sw.d[i].hash == 0) {
            block = &gpu->vshaders_sw.d[i];
            break;
        }
    }
    if (!block) {
        block = LRU_eject(gpu->vshaders_sw);
    }
    LRU_use(gpu->vshaders_sw, block);
    if (block->hash != hash) {
        block->hash = hash;
        shaderjit_backend_free(block->backend);
        block->backend = shaderjit_backend_init();
    }
    return shaderjit_backend_get_code(block->backend, shu);
}

void shaderjit_free_all(GPU* gpu) {
    for (int i = 0; i < VSH_MAX; i++) {
        shaderjit_backend_free(gpu->vshaders_sw.d[i].backend);
    }
}