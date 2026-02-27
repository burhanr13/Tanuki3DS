#ifndef SERVICES_H
#define SERVICES_H

#include "kernel/thread.h"

#include "am.h"
#include "apt.h"
#include "boss.h"
#include "cecd.h"
#include "cfg.h"
#include "dsp.h"
#include "frd.h"
#include "fs.h"
#include "gsp.h"
#include "hid.h"
#include "ir.h"
#include "ldr.h"
#include "nwm.h"
#include "ptm.h"
#include "y2r.h"

typedef struct {

    KSemaphore notif_sem;

    APTData apt;
    GSPData gsp;
    HIDData hid;
    DSPData dsp;
    FSData fs;
    CECDData cecd;
    Y2RData y2r;
    LDRData ldr;
    IRData ir;

} ServiceData;

#endif
