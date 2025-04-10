#ifndef THREAD_H
#define THREAD_H

#include "common.h"
#include "scheduler.h"

#include "kernel.h"
#include "memory.h"

#define THREAD_MAX 32

#define TIMEOUT 0x09401bfe

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
            alignas(16) u32 r[16];
            struct {
                u32 arg;
                u32 _r[12];
                u32 sp;
                u32 lr;
                u32 pc;
            };
        };
        u32 cpsr;

        alignas(16) double d[16];
        u32 fpscr;
    } ctx;

    u32 waiting_addr;
    KListNode* waiting_objs;
    bool wait_any;

    KListNode* waiting_thrds;

    KListNode* owned_mutexes;

    struct _KThread *next, *prev; // in the ready list

    u32 id;
    s32 priority;
    u32 state;
    u32 cpu; // 0: appcore, 1: syscore, 2: new3ds appcore2
    u32 tls;
} KThread;

typedef void (*KEventCallback)(E3DS*);

typedef struct {
    KObject hdr;

    bool signal;
    bool sticky;

    KListNode* waiting_thrds;

    KEventCallback callback;
} KEvent;

typedef struct {
    KObject hdr;

    bool signal;
    bool sticky;
    bool repeat;

    s64 interval;

    KListNode* waiting_thrds;
} KTimer;

typedef struct {
    KObject hdr;
    KListNode* waiting_thrds;
    s32 count;
    s32 max;
} KSemaphore;

typedef struct {
    KObject hdr;

    KThread* locker_thrd;
    u32 recursive_lock_count;

    KListNode* waiting_thrds;
} KMutex;

typedef struct {
    KObject hdr;

    KListNode* waiting_thrds;
} KArbiter;

#define THRD_MAX_PRIO 0x40

#define CUR_THREAD ((KThread*) s->process.handles[0])

void e3ds_restore_context(E3DS* s);
void e3ds_save_context(E3DS* s);

void thread_init(E3DS* s, u32 entrypoint);
KThread* thread_create(E3DS* s, u32 entrypoint, u32 stacktop, u32 priority,
                       u32 arg, s32 processorID);
void thread_ready(E3DS* s, KThread* t);
void thread_reschedule(E3DS* s);

void thread_sleep(E3DS* s, KThread* t, s64 timeout);
void thread_wakeup_timeout(E3DS* s, KThread* t);
bool thread_wakeup(E3DS* s, KThread* t, KObject* reason);

void thread_kill(E3DS* s, KThread* t);

KEvent* event_create(bool sticky);
void event_signal(E3DS* s, KEvent* ev);

KTimer* timer_create_(bool sticky, bool repeat);
void timer_signal(E3DS* s, KTimer* tmr);

KMutex* mutex_create();
void mutex_release(E3DS* s, KMutex* mtx);

KSemaphore* semaphore_create(s32 init, s32 max);
void semaphore_release(E3DS* s, KSemaphore* sem, s32 count);

bool sync_wait(E3DS* s, KThread* t, KObject* o);
void sync_cancel(KThread* t, KObject* o);

#endif
