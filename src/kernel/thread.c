#include "thread.h"

#include "3ds.h"

void e3ds_restore_context(E3DS* s) {
    if (!CUR_THREAD) return;
    memcpy(s->cpu.r, CUR_THREAD->ctx.r, sizeof s->cpu.r);
    memcpy(s->cpu.d, CUR_THREAD->ctx.d, sizeof s->cpu.d);
    s->cpu.cpsr.w = CUR_THREAD->ctx.cpsr;
    s->cpu.fpscr.w = CUR_THREAD->ctx.fpscr;
}

void e3ds_save_context(E3DS* s) {
    if (!CUR_THREAD) return;
    memcpy(CUR_THREAD->ctx.r, s->cpu.r, sizeof s->cpu.r);
    memcpy(CUR_THREAD->ctx.d, s->cpu.d, sizeof s->cpu.d);
    CUR_THREAD->ctx.cpsr = s->cpu.cpsr.w;
    CUR_THREAD->ctx.fpscr = s->cpu.fpscr.w;
}

void thread_init(E3DS* s, u32 entrypoint) {
    s->readylist.priority = 0;
    s->readylist.id = 0;
    s->readylist.next = &s->readylist;
    s->readylist.prev = &s->readylist;

    s->process.nexttid = 0;
    s->process.allocTls = 0;

    KThread* thd = thread_create(s, entrypoint, STACK_BASE, 0x30, 0, 0);

    s->process.handles[0] = &thd->hdr;
    s->process.handles[0]->refcount = 2;

    // manually schedule the main thread
    thd->next->prev = thd->prev;
    thd->prev->next = thd->next;
    thd->next = nullptr;
    thd->prev = nullptr;
    thd->state = THRD_RUNNING;
}

KThread* thread_create(E3DS* s, u32 entrypoint, u32 stacktop, u32 priority,
                       u32 arg, s32 processorID) {

    KThread* thrd = calloc(1, sizeof *thrd);
    thrd->hdr.type = KOT_THREAD;
    thrd->ctx.arg = arg;
    thrd->ctx.sp = stacktop;
    thrd->ctx.pc = entrypoint;
    thrd->ctx.cpsr = M_USER;
    thrd->priority = priority;
    thrd->id = s->process.nexttid++;
    thrd->state = THRD_SLEEP;
    if (processorID < 0) { // negative means the kernel picks a cpu
        thrd->cpu = 0;
    } else {
        thrd->cpu = processorID;
        if (thrd->cpu > 0) {
            lwarn("scheduling thread on core %d", thrd->cpu);
        }
    }

    for (int i = 0; i < 32; i++) {
        if (!(s->process.allocTls & BIT(i))) {
            s->process.allocTls |= BIT(i);
            thrd->tls = TLS_BASE + TLS_SIZE * i;
            break;
        }
    }

    if (!thrd->tls) lerror("not enough TLS slots");

    thread_ready(s, thrd);

    linfo("creating thread %d (entry %08x, stack %08x, priority %#x, arg %x)",
          thrd->id, entrypoint, stacktop, priority, arg);
    return thrd;
}

void thread_ready(E3DS* s, KThread* t) {
    if (t->state == THRD_READY || t->next || t->prev) {
        lerror("thread already ready (this should never happen)");
        return;
    }

    t->state = THRD_READY;

    t->next = &s->readylist;
    t->prev = s->readylist.prev;
    t->next->prev = t;
    t->prev->next = t;

    while (t->priority < t->prev->priority) {
        t->prev->next = t->next;
        t->next->prev = t->prev;
        t->next = t->prev;
        t->prev = t->prev->prev;
        t->prev->next = t;
        t->next->prev = t;
    }
}

void thread_reschedule(E3DS* s) {
    KThread* cur = CUR_THREAD;
    KThread* next;

    if (cur && cur->state == THRD_READY) {
        lerror("cur thread should never be ready");
        return;
    }

    // find the next thread
    if (cur && cur->state == THRD_RUNNING) {
        if (s->readylist.next == &s->readylist) {
            // no other threads are ready so dont switch
            next = cur;
        } else {
            // only switch to a new thread if its priority is
            // higher than the current thread
            if (s->readylist.next->priority < CUR_THREAD->priority) {
                next = s->readylist.next;
            } else {
                next = cur;
            }
        }
    } else {
        if (s->readylist.next == &s->readylist) {
            // no more threads are ready right now
            next = nullptr;
        } else {
            next = s->readylist.next;
        }
    }

    if (CUR_THREAD == next) {
        linfo("not switching threads");
        return;
    }

    // put cur thread into ready list if necessary
    if (cur && cur->state == THRD_RUNNING) thread_ready(s, CUR_THREAD);

    if (next) {
        // pop from the ready list
        next->next->prev = next->prev;
        next->prev->next = next->next;
        next->next = nullptr;
        next->prev = nullptr;
        next->state = THRD_RUNNING;
        s->cpu.halt = false;
    } else {
        s->cpu.halt = true;
    }

    if (cur) s->process.handles[0]->refcount--;
    s->process.handles[0] = &next->hdr;
    if (next) s->process.handles[0]->refcount++;

    if (cur && next) {
        linfo("switching from thread %d to thread %d", cur->id, next->id);
    } else if (next) {
        linfo("switching from idle to thread %d", next->id);
    } else {
        linfo("switching from thread %d to idle", cur->id);
    }
}

void thread_sleep(E3DS* s, KThread* t, s64 timeout) {
    linfo("sleeping thread %d with timeout %ld", t->id, timeout);

    if (timeout == 0) {
        // instantly wakup the thread and set the return to timeout
        // without rescheduling
        // waitsync with timeout=0 is used to poll a sync object
        KListNode** cur = &t->waiting_objs;
        if (*cur) t->ctx.r[0] = TIMEOUT;
        while (*cur) {
            sync_cancel(t, (*cur)->key);
            klist_remove(cur);
        }
        return;
    }

    t->state = THRD_SLEEP;
    if (timeout > 0) {
        add_event(&s->sched, (SchedulerCallback) thread_wakeup_timeout, t,
                  NS_TO_CYCLES(timeout));
    }
    thread_reschedule(s);
}

void thread_wakeup_timeout(E3DS* s, KThread* t) {
    if (t->state != THRD_SLEEP) {
        lerror("thread already awake (this should never happen)");
        return;
    }

    linfo("waking up thread %d from timeout", t->id);
    KListNode** cur = &t->waiting_objs;
    if (*cur) t->ctx.r[0] = TIMEOUT;
    while (*cur) {
        sync_cancel(t, (*cur)->key);
        klist_remove(cur);
    }
    thread_ready(s, t);
    thread_reschedule(s);
}

bool thread_wakeup(E3DS* s, KThread* t, KObject* reason) {
    if (t->state != THRD_SLEEP) {
        lerror("thread already awake (this should never happen)");
        return false;
    }
    u32 val = klist_remove_key(&t->waiting_objs, reason);
    if (t->wait_any) t->ctx.r[1] = val;
    if (!t->waiting_objs || t->wait_any) {
        linfo("waking up thread %d", t->id);
        KListNode** cur = &t->waiting_objs;
        while (*cur) {
            sync_cancel(t, (*cur)->key);
            klist_remove(cur);
        }
        remove_event(&s->sched, (SchedulerCallback) thread_wakeup_timeout, t);
        thread_ready(s, t);
        thread_reschedule(s);
        return true;
    }
    return false;
}

void thread_kill(E3DS* s, KThread* t) {
    if (t->state == THRD_DEAD) return;

    linfo("killing thread %d", t->id);

    s->process.allocTls &= ~BIT((t->tls - TLS_BASE) / TLS_SIZE);

    if (t->state == THRD_READY) {
        t->next->prev = t->prev;
        t->prev->next = t->next;
    }

    t->state = THRD_DEAD;

    auto cur = &t->waiting_thrds;
    while (*cur) {
        thread_wakeup(s, (KThread*) (*cur)->key, &t->hdr);
        klist_remove(cur);
    }

    cur = &t->owned_mutexes;
    while (*cur) {
        mutex_release(s, (KMutex*) (*cur)->key);
        // don't remove the list node since mutex release does that
    }

    cur = &t->waiting_objs;
    while (*cur) {
        sync_cancel(t, (*cur)->key);
        klist_remove(cur);
    }

    remove_event(&s->sched, (SchedulerCallback) thread_wakeup_timeout, t);

    thread_reschedule(s);
}

KThread* remove_highest_prio(KListNode** l) {
    int maxprio = THRD_MAX_PRIO;
    KListNode** cur = l;
    KListNode** toremove = nullptr;
    KThread* wakeupthread = nullptr;
    while (*cur) {
        KThread* t = (KThread*) (*cur)->key;
        if (t->priority < maxprio) {
            maxprio = t->priority;
            toremove = cur;
            wakeupthread = t;
        }
        cur = &(*cur)->next;
    }
    if (toremove) klist_remove(toremove);
    return wakeupthread;
}

KEvent* event_create(bool sticky) {
    KEvent* ev = calloc(1, sizeof *ev);
    ev->hdr.type = KOT_EVENT;
    ev->sticky = sticky;
    return ev;
}

void event_signal(E3DS* s, KEvent* ev) {
    if (ev->sticky) {
        KListNode** cur = &ev->waiting_thrds;
        while (*cur) {
            thread_wakeup(s, (KThread*) (*cur)->key, &ev->hdr);
            klist_remove(cur);
        }
        ev->signal = true;
    } else {
        KThread* thr = remove_highest_prio(&ev->waiting_thrds);
        if (thr) thread_wakeup(s, thr, &ev->hdr);
    }
    if (ev->callback) ev->callback(s);
}

KTimer* timer_create_(bool sticky, bool repeat) {
    KTimer* tmr = calloc(1, sizeof *tmr);
    tmr->hdr.type = KOT_TIMER;
    tmr->sticky = sticky;
    tmr->repeat = repeat;
    return tmr;
}

void timer_signal(E3DS* s, KTimer* tmr) {
    if (tmr->sticky) {
        KListNode** cur = &tmr->waiting_thrds;
        while (*cur) {
            thread_wakeup(s, (KThread*) (*cur)->key, &tmr->hdr);
            klist_remove(cur);
        }
        tmr->signal = true;
    } else {
        KThread* thr = remove_highest_prio(&tmr->waiting_thrds);
        if (thr) thread_wakeup(s, thr, &tmr->hdr);
    }

    if (tmr->repeat) {
        add_event(&s->sched, (SchedulerCallback) timer_signal, tmr,
                  NS_TO_CYCLES(tmr->interval));
    }
}

KMutex* mutex_create() {
    KMutex* mtx = calloc(1, sizeof *mtx);
    mtx->hdr.type = KOT_MUTEX;
    return mtx;
}

void mutex_release(E3DS* s, KMutex* mtx) {
    if (!mtx->locker_thrd) {
        // this mutex was not locked already
        return;
    }

    if (mtx->recursive_lock_count > 0) {
        // this mutex was recursively locked so decrement the count only
        mtx->recursive_lock_count--;
        return;
    }

    klist_remove_key(&mtx->locker_thrd->owned_mutexes, &mtx->hdr);

    // no threads are waiting so this mutex is now free
    if (!mtx->waiting_thrds) {
        mtx->locker_thrd = nullptr;
        return;
    }

    // find the highest priority thread waiting on this mutex
    // give it the lock and wake it up
    KThread* wakeupthread = remove_highest_prio(&mtx->waiting_thrds);
    thread_wakeup(s, wakeupthread, &mtx->hdr);
    mtx->locker_thrd = wakeupthread;
    klist_insert(&wakeupthread->owned_mutexes, &mtx->hdr);
}

KSemaphore* semaphore_create(s32 init, s32 max) {
    KSemaphore* sem = calloc(1, sizeof *sem);
    sem->hdr.type = KOT_SEMAPHORE;
    sem->count = init;
    sem->max = max;
    return sem;
}

void semaphore_release(E3DS* s, KSemaphore* sem, s32 count) {
    sem->count += count;
    if (sem->count > sem->max) sem->count = sem->max;

    while (sem->count > 0 && sem->waiting_thrds) {
        sem->count--;
        KThread* wakeupthread = remove_highest_prio(&sem->waiting_thrds);
        thread_wakeup(s, wakeupthread, &sem->hdr);
    }
}

bool sync_wait(E3DS* s, KThread* t, KObject* o) {
    switch (o->type) {
        case KOT_THREAD: {
            auto thr = (KThread*) o;
            if (thr->state == THRD_DEAD) return false;
            klist_insert(&thr->waiting_thrds, &t->hdr);
            return true;
        }
        case KOT_EVENT: {
            auto event = (KEvent*) o;
            if (event->signal) return false;
            klist_insert(&event->waiting_thrds, &t->hdr);
            return true;
        }
        case KOT_TIMER: {
            auto tmr = (KTimer*) o;
            if (tmr->signal) return false;
            klist_insert(&tmr->waiting_thrds, &t->hdr);
            return true;
        }
        case KOT_MUTEX: {
            auto mtx = (KMutex*) o;
            if (mtx->locker_thrd && mtx->locker_thrd != t) {
                klist_insert(&mtx->waiting_thrds, &t->hdr);
                return true;
            }
            if (mtx->locker_thrd != t) {
                mtx->locker_thrd = t;
                klist_insert(&t->owned_mutexes, &mtx->hdr);
            } else {
                // this thread is locking the mutex more than once
                // we need to record this since release mutex
                // now needs to be called multiple times as well
                mtx->recursive_lock_count++;
            }
            return false;
        }
        case KOT_SEMAPHORE: {
            auto sem = (KSemaphore*) o;
            if (sem->count > 0) {
                sem->count--;
                return false;
            }
            klist_insert(&sem->waiting_thrds, &t->hdr);
            return true;
        }
        default:
            t->ctx.r[0] = -1;
            lerror("cannot wait on this %d", o->type);
            return false;
    }
}

void sync_cancel(KThread* t, KObject* o) {
    switch (o->type) {
        case KOT_EVENT: {
            auto event = (KEvent*) o;
            klist_remove_key(&event->waiting_thrds, &t->hdr);
            break;
        }
        case KOT_MUTEX: {
            auto mutex = (KMutex*) o;
            klist_remove_key(&mutex->waiting_thrds, &t->hdr);
            break;
        }
        case KOT_THREAD: {
            auto thread = (KThread*) o;
            klist_remove_key(&thread->waiting_thrds, &t->hdr);
            break;
        }
        case KOT_SEMAPHORE: {
            auto sem = (KSemaphore*) o;
            klist_remove_key(&sem->waiting_thrds, &t->hdr);
            break;
        }
        case KOT_TIMER: {
            auto tmr = (KTimer*) o;
            klist_remove_key(&tmr->waiting_thrds, &t->hdr);
            break;
        }
        case KOT_ARBITER: {
            auto arb = (KArbiter*) o;
            klist_remove_key(&arb->waiting_thrds, &t->hdr);
            break;
        }
        default:
            lerror("unknown sync object %d", o->type);
            break;
    }
}
