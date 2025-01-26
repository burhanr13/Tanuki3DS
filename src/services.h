#ifndef SERVICES_H
#define SERVICES_H

#include "services/apt.h"
#include "services/cecd.h"
#include "services/cfg.h"
#include "services/dsp.h"
#include "services/fs.h"
#include "services/gsp.h"
#include "services/hid.h"
#include "services/y2r.h"

#include "thread.h"

typedef struct {

    KSemaphore notif_sem;

    APTData apt;
    GSPData gsp;
    HIDData hid;
    DSPData dsp;
    FSData fs;
    CECDData cecd;

} ServiceData;

#endif