#ifndef THREAD_H
#define THREAD_H

#include "kernel.h"
#include "memory.h"
#include "types.h"

#define THREAD_MAX 32

typedef struct _3DS HLE3DS;

enum {
    THRD_RUNNING,
    THRD_READY,
    THRD_SLEEP,
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
    } context;

    u32 waiting_addr;
    KListNode* waiting_objs;
    bool wait_all;

    u32 id;
    s32 priority;
    u32 state;
} KThread;

typedef struct _KProcess {
    KObject hdr;

    VMBlock vmblocks;

    KObject* handles[HANDLE_MAX];

    KThread* threads[THREAD_MAX];

    u32 used_memory;
} KProcess;

typedef void (*KEventCallback)(HLE3DS*, u32);

typedef struct {
    KObject hdr;

    bool signal;
    bool sticky;

    KListNode* waiting_thrds;

    KEventCallback callback;
} KEvent;

typedef struct {
    KObject hdr;
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
#define CUR_TLS (TLS_BASE + TLS_SIZE * (CUR_THREAD->id))

void thread_init(HLE3DS* s, u32 entrypoint);
u32 thread_create(HLE3DS* s, u32 entrypoint, u32 stacktop, u32 priority,
                  u32 arg);
bool thread_reschedule(HLE3DS* s);

void thread_sleep(HLE3DS* s, KThread* t, s64 timeout);
void thread_wakeup_timeout(HLE3DS* s, u32 tid);
bool thread_wakeup(KThread* t, KObject* reason);

KEvent* event_create(bool sticky);
void event_signal(HLE3DS* s, KEvent* ev);

KMutex* mutex_create();
void mutex_release(HLE3DS* s, KMutex* mtx);

bool sync_wait(HLE3DS* s, KObject* o);
void sync_cancel(KThread* t, KObject* o);

#endif
