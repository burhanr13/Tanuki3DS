#ifndef CECD_H
#define CECD_H

#include "../srv.h"
#include "../thread.h"

typedef struct {
    KEvent cecinfo;
} CECDData;

DECL_PORT(cecd);

#endif
