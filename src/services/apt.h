#ifndef APT_H
#define APT_H

#include "../srv.h"
#include "../thread.h"

enum {
    APTCMD_NONE,
    APTCMD_WAKEUP,
    APTCMD_REQUEST,
    APTCMD_RESPONSE,
};

enum {
    APPID_HOMEMENU = 0x101,
    APPID_MIISELECTOR = 0x402,
    APPID_ERRDISP = 0x406,
};

typedef struct {
    KMutex lock;
    KEvent notif_event;
    KEvent resume_event;
    KSharedMem shared_font;
    KSharedMem capture_block;

    int application_cpu_time_limit;

    struct {
        u32 appid;
        u32 cmd;
        KObject* kobj;
    } nextparam;
} APTData;

DECL_PORT(apt);

#endif
