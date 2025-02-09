#ifndef SHADERDEC_H
#define SHADERDEC_H

#include "shader.h"

typedef struct _GPU GPU;

typedef struct {
    fvec c[96];
    int i[4][4];
    int b_raw;
} VertUniforms;

char* shader_dec_vs(GPU* gpu);

#endif
