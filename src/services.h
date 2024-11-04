#ifndef SERVICE_DATA_H
#define SERVICE_DATA_H

#include "services/ac.h"
#include "services/am.h"
#include "services/apt.h"
#include "services/boss.h"
#include "services/cam.h"
#include "services/cecd.h"
#include "services/cfg.h"
#include "services/dsp.h"
#include "services/frd.h"
#include "services/fs.h"
#include "services/gsp.h"
#include "services/hid.h"
#include "services/mic.h"
#include "services/ndm.h"
#include "services/nim.h"
#include "services/ptm.h"
#include "services/y2r.h"

#include "thread.h"

typedef struct {

    KSemaphore notif_sem;

    APTData apt;
    GSPData gsp;
    HIDData hid;
    DSPData dsp;
    FSData fs;
    MicData mic;
    BOSSData boss;

} ServiceData;

#endif