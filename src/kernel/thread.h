#ifndef THREAD_H
#define THREAD_H

#include <common.h>

#include "kernel.h"
#include "memory.h"

#define THREAD_MAX 32

typedef struct _3DS E3DS;

enum {
    THRD_RUNNING,
    THRD_READY,
    THRD_SLEEP,
    THRD_DEAD,
};

typedef struct _KThread {
    KObject hdr;

    struct {
        union {
            u32 r[16];
            struct {
                u32 arg;
                u32 _r[12];
                u32 sp;
                u32 lr;
                u32 pc;
            };
        };
        u32 cpsr;

        double d[16];
        u32 fpscr;
    } ctx;

    u32 waiting_addr;
    KListNode* waiting_objs;
    bool wait_all;

    KListNode* waiting_thrds;

    u32 id;
    s32 priority;
    u32 state;
} KThread;

typedef void (*KEventCallback)(E3DS*, u32);

typedef struct {
    KObject hdr;

    bool signal;
    bool sticky;

    KListNode* waiting_thrds;

    KEventCallback callback;
} KEvent;

typedef struct {
    KObject hdr;
    KListNode* waiting_thrds;
} KSemaphore;

typedef struct {
    KObject hdr;

    KThread* locker_thrd;

    KListNode* waiting_thrds;
} KMutex;

typedef struct {
    KObject hdr;

    KListNode* waiting_thrds;
} KArbiter;

#define THRD_MAX_PRIO 0x40

#define CUR_THREAD ((KThread*) s->process.handles[0])

#define GETTLS(t) (TLS_BASE + TLS_SIZE * (t)->id)

void e3ds_restore_context(E3DS* s);
void e3ds_save_context(E3DS* s);

void thread_init(E3DS* s, u32 entrypoint);
u32 thread_create(E3DS* s, u32 entrypoint, u32 stacktop, u32 priority, u32 arg);
void thread_reschedule(E3DS* s);

void thread_sleep(E3DS* s, KThread* t, s64 timeout);
void thread_wakeup_timeout(E3DS* s, u32 tid);
bool thread_wakeup(E3DS* s, KThread* t, KObject* reason);

void thread_kill(E3DS* s, KThread* t);

KEvent* event_create(bool sticky);
void event_signal(E3DS* s, KEvent* ev);

KMutex* mutex_create();
void mutex_release(E3DS* s, KMutex* mtx);

bool sync_wait(E3DS* s, KThread* t, KObject* o);
void sync_cancel(KThread* t, KObject* o);

#endif
