#ifndef SVC_DEFS_H
#define SVC_DEFS_H

#include "common.h"

enum {
    MEMOP_ALLOC = 3,
    MEMOP_MASK = 0xff,

    MEMOP_REGMASK = 0xf00,

    MEMOP_LINEAR = 0x10000,
};

enum {
    PERM_R = BIT(0),
    PERM_W = BIT(1),
    PERM_X = BIT(2),
    PERM_RW = PERM_R | PERM_W,
    PERM_RX = PERM_R | PERM_X,
};

enum {
    MEMST_FREE = 0,
    MEMST_STATIC = 3,
    MEMST_CODE = 4,
    MEMST_PRIVATE = 5,
    MEMST_SHARED = 6,
};

enum {
    RESET_ONESHOT,
    RESET_STICKY,
    RESET_PULSE,
};

enum {
    ARBITRATE_SIGNAL,
    ARBITRATE_WAIT,
    ARBITRATE_DEC_WAIT,
    ARBITRATE_WAIT_TIMEOUT,
    ARBITRATE_DEC_WAIT_TIMEOUT,
};

enum {
    RES_MEMORY = 1,
};

#endif
