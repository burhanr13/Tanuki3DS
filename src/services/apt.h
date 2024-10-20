#ifndef APT_H
#define APT_H

#include "../srv.h"
#include "../thread.h"

typedef struct {
    KMutex lock;
    KEvent notif_event;
    KEvent resume_event;
    int application_cpu_time_limit;
} APTData;

DECL_PORT(apt);

#endif
