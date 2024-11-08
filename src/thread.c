#include "thread.h"

#include "3ds.h"

void load_context(HLE3DS* s) {
    for (int i = 0; i < 16; i++) {
        s->cpu.r[i] = CUR_THREAD->context.r[i];
        s->cpu.d[i] = CUR_THREAD->context.d[i];
    }
    s->cpu.cpsr.w = CUR_THREAD->context.cpsr;
    s->cpu.fpscr.w = CUR_THREAD->context.fpscr;
}

void save_context(HLE3DS* s) {
    for (int i = 0; i < 16; i++) {
        CUR_THREAD->context.r[i] = s->cpu.r[i];
        CUR_THREAD->context.d[i] = s->cpu.d[i];
    }
    CUR_THREAD->context.cpsr = s->cpu.cpsr.w;
    CUR_THREAD->context.fpscr = s->cpu.fpscr.w;
}

void thread_init(HLE3DS* s, u32 entrypoint) {
    u32 tid = thread_create(s, entrypoint, STACK_BASE, 0x30, 0);

    s->process.handles[0] = &s->process.threads[tid]->hdr;
    s->process.handles[0]->refcount = 2;

    load_context(s);
    CUR_THREAD->state = THRD_RUNNING;
}

u32 thread_create(HLE3DS* s, u32 entrypoint, u32 stacktop, u32 priority,
                  u32 arg) {
    u32 tid = -1;
    for (int i = 0; i < THREAD_MAX; i++) {
        if (!s->process.threads[i]) {
            tid = i;
            break;
        }
    }
    if (tid == -1) {
        lerror("not enough threads");
        return tid;
    }
    KThread* thrd = calloc(1, sizeof *thrd);
    thrd->hdr.type = KOT_THREAD;
    thrd->context.arg = arg;
    thrd->context.sp = stacktop;
    thrd->context.pc = entrypoint;
    thrd->context.cpsr = M_USER;
    thrd->priority = priority;
    thrd->state = THRD_READY;
    thrd->id = tid;
    s->process.threads[tid] = thrd;

    linfo("creating thread %d (entry %08x, stack %08x, priority %x, arg %x)",
          tid, entrypoint, stacktop, priority, arg);
    return tid;
}

bool thread_reschedule(HLE3DS* s) {
    if (CUR_THREAD->state == THRD_RUNNING) CUR_THREAD->state = THRD_READY;
    u32 nexttid = -1;
    u32 maxprio = THRD_MAX_PRIO;
    for (u32 i = 0; i < THREAD_MAX; i++) {
        KThread* t = s->process.threads[i];
        if (!t) continue;
        if (t->state == THRD_READY && t->priority < maxprio) {
            maxprio = t->priority;
            nexttid = i;
        }
    }
    if (nexttid == -1) {
        s->cpu.wfe = true;
        linfo("all threads sleeping");
        return false;
    } else {
        s->cpu.wfe = false;
    }

    if (CUR_THREAD->id == nexttid) {
        linfo("not switching threads");
    } else {
        linfo("switching from thread %d to thread %d", CUR_THREAD->id, nexttid);
        save_context(s);
        s->process.handles[0]->refcount--;
        s->process.handles[0] = &s->process.threads[nexttid]->hdr;
        s->process.handles[0]->refcount++;
        load_context(s);
    }
    return true;
}

void thread_sleep(HLE3DS* s, KThread* t, s64 timeout) {
    linfo("sleeping thread %d with timeout %ld", t->id, timeout);
    if (timeout < 0) {
        t->state = THRD_SLEEP;
    } else if (timeout > 0) {
        t->state = THRD_SLEEP;
        s64 timeCycles = timeout * CPU_CLK / 1000000000;
        add_event(&s->sched, thread_wakeup_timeout, t->id, timeCycles);
    }
}

void thread_wakeup_timeout(HLE3DS* s, u32 tid) {
    KThread* t = s->process.threads[tid];
    if (!t || t->state != THRD_SLEEP) return;

    linfo("waking up thread %d from timeout", t->id);
    KListNode** cur = &t->waiting_objs;
    while (*cur) {
        sync_cancel(t, (*cur)->key);
        klist_remove(cur);
    }
    t->state = THRD_READY;
    thread_reschedule(s);
}

bool thread_wakeup(HLE3DS* s, KThread* t, KObject* reason) {
    if (t->state != THRD_SLEEP) return false;
    t->context.r[1] = klist_remove_key(&t->waiting_objs, reason);
    if (!t->waiting_objs || !t->wait_all) {
        linfo("waking up thread %d", t->id);
        KListNode** cur = &t->waiting_objs;
        while (*cur) {
            sync_cancel(t, (*cur)->key);
            klist_remove(cur);
        }
        remove_event(&s->sched, thread_wakeup_timeout, t->id);
        t->state = THRD_READY;
        return true;
    }
    return false;
}

KEvent* event_create(bool sticky) {
    KEvent* ev = calloc(1, sizeof *ev);
    ev->hdr.type = KOT_EVENT;
    ev->sticky = sticky;
    return ev;
}

void event_signal(HLE3DS* s, KEvent* ev) {
    KListNode** cur = &ev->waiting_thrds;
    while (*cur) {
        thread_wakeup(s, (KThread*) (*cur)->key, &ev->hdr);
        klist_remove(cur);
    }
    if (ev->callback) ev->callback(s, 0);
    thread_reschedule(s);
    if (ev->sticky) ev->signal = true;
}

KMutex* mutex_create() {
    KMutex* mtx = calloc(1, sizeof *mtx);
    mtx->hdr.type = KOT_MUTEX;
    return mtx;
}

void mutex_release(HLE3DS* s, KMutex* mtx) {
    if (!mtx->waiting_thrds) {
        mtx->locker_thrd = NULL;
        return;
    }
    if (!mtx->locker_thrd) return;

    int maxprio = THRD_MAX_PRIO;
    KListNode** cur = &mtx->waiting_thrds;
    KListNode** toremove = NULL;
    KThread* wakeupthread = NULL;
    while (*cur) {
        KThread* t = (KThread*) (*cur)->key;
        if (t->priority < maxprio) {
            maxprio = t->priority;
            toremove = cur;
            wakeupthread = t;
        }
        cur = &(*cur)->next;
    }
    thread_wakeup(s, wakeupthread, &mtx->hdr);
    klist_remove(toremove);
    mtx->locker_thrd = wakeupthread;

    thread_reschedule(s);
}

bool sync_wait(HLE3DS* s, KObject* o) {
    switch (o->type) {
        case KOT_THREAD: {
            KThread* thr = (KThread*) o;
            if (thr->state == THRD_DEAD) return false;
            klist_insert(&thr->waiting_thrds, &CUR_THREAD->hdr);
            return true;
        }
        case KOT_EVENT: {
            KEvent* event = (KEvent*) o;
            if (event->signal) return false;
            klist_insert(&event->waiting_thrds, &CUR_THREAD->hdr);
            return true;
        }
        case KOT_MUTEX: {
            KMutex* mtx = (KMutex*) o;
            if (mtx->locker_thrd && mtx->locker_thrd != CUR_THREAD) {
                klist_insert(&mtx->waiting_thrds, &CUR_THREAD->hdr);
                return true;
            }
            mtx->locker_thrd = CUR_THREAD;
            return false;
        }
        case KOT_SEMAPHORE: {
            return true;
        }
        default:
            R(0) = -1;
            lerror("cannot wait on this %d", o->type);
            return false;
    }
}

void sync_cancel(KThread* t, KObject* o) {
    switch (o->type) {
        case KOT_EVENT: {
            KEvent* event = (KEvent*) o;
            klist_remove_key(&event->waiting_thrds, &t->hdr);
            break;
        }
        case KOT_MUTEX: {
            KMutex* mutex = (KMutex*) o;
            klist_remove_key(&mutex->waiting_thrds, &t->hdr);
            break;
        }
        case KOT_THREAD: {
            KThread* thread = (KThread*) o;
            klist_remove_key(&thread->waiting_thrds, &t->hdr);
            break;
        }
        default:
            break;
    }
}
