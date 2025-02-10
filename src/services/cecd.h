#ifndef CECD_H
#define CECD_H

#include <kernel/thread.h>

#include "srv.h"

typedef struct {
    KEvent cecinfo;
} CECDData;

DECL_PORT(cecd);

#endif
