#ifndef BOSS_H
#define BOSS_H

#include "../srv.h"

typedef struct {
    s8 optoutflag;
} BOSSData;

DECL_PORT(boss);

#endif
