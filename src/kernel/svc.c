#include "svc.h"

#include <stdlib.h>
#include <string.h>

#include "emulator.h"
#include "services/srv.h"

#include "svc_types.h"
#include "thread.h"

#define R(n) caller->ctx.r[n]

#define MAKE_HANDLE(handle)                                                    \
    u32 handle = handle_new(s);                                                \
    if (!handle) {                                                             \
        R(0) = -1;                                                             \
        return;                                                                \
    }

void e3ds_handle_svc(E3DS* s, u32 num) {
    e3ds_save_context(s);

    KThread* caller = CUR_THREAD;
    num &= 0xff;
    if (!svc_table[num]) {
        lerror("unknown svc 0x%x (0x%x,0x%x,0x%x,0x%x) at %08x (lr=%08x)", num,
               R(0), R(1), R(2), R(3), R(15), R(14));
    } else {
        linfo("svc 0x%x: %s(0x%x,0x%x,0x%x,0x%x,0x%x) at %08x (lr=%08x)", num,
              svc_names[num], R(0), R(1), R(2), R(3), R(4), R(15), R(14));
        svc_table[num](s, caller);
    }

    e3ds_restore_context(s);
}

DECL_SVC(ControlMemory) {
    u32 memop = R(0) & MEMOP_MASK;
    [[maybe_unused]] u32 memreg = R(0) & MEMOP_REGMASK;
    bool linear = R(0) & MEMOP_LINEAR;
    u32 addr0 = R(1);
    u32 addr1 = R(2);
    u32 size = R(3);
    u32 perm = R(4);

    R(0) = 0;
    switch (memop) {
        case MEMOP_ALLOC:
            if (linear) {
                R(1) = memory_linearheap_grow(s, size, perm);
            } else {
                R(1) = memory_virtalloc(s, addr0, size, perm, MEMST_PRIVATE);
            }
            break;
        case MEMOP_MIRRORMAP:
            R(1) = memory_virtmirror(s, addr1, addr0, size, perm);
            break;
        default:
            lwarn("unknown memory op %d addr0=%08x addr1=%08x size=%x", memop,
                  addr0, addr1, size);
            R(0) = 0;
            R(1) = addr0;
            break;
    }
}

DECL_SVC(QueryMemory) {
    u32 addr = R(2);
    VMBlock* b = memory_virtquery(s, addr);
    R(0) = 0;
    R(1) = b->startpg << 12;
    R(2) = (b->endpg - b->startpg) << 12;
    R(3) = b->perm;
    R(4) = b->state;
    R(5) = 0;
}

DECL_SVC(ExitProcess) {
    lerror("process exiting");
    longjmp(ctremu.exceptionJmp, 1);
}

DECL_SVC(CreateThread) {
    s32 priority = R(0);
    u32 entrypoint = R(1);
    u32 arg = R(2);
    u32 stacktop = R(3);
    s32 processor = R(4);

    MAKE_HANDLE(handle);

    if (priority < 0 || priority >= THRD_MAX_PRIO) {
        R(0) = -1;
        return;
    }
    stacktop &= ~7;

    KThread* t =
        thread_create(s, entrypoint, stacktop, priority, arg, processor);

    HANDLE_SET(handle, t);
    t->hdr.refcount = 1;
    linfo("created thread %d with handle %x", t->id, handle);

    thread_reschedule(s);

    R(0) = 0;
    R(1) = handle;
}

DECL_SVC(ExitThread) {
    linfo("thread %d exiting", caller->id);

    thread_kill(s, caller);
}

DECL_SVC(SleepThread) {
    s64 timeout = R(0) | (u64) R(1) << 32;

    if (timeout == 0) {
        // this acts as thread yield, this thread will stay
        // ready but let another thread run for now
        // importantly if no other threads are ready we do nothing
        if (s->readylist.next != &s->readylist) {
            caller->state = THRD_SLEEP;
            thread_reschedule(s);
            thread_ready(s, caller);
        }
    } else {
        thread_sleep(s, caller, timeout);
    }

    R(0) = 0;
}

DECL_SVC(GetThreadPriority) {
    KThread* t = HANDLE_GET_TYPED(R(1), KOT_THREAD);
    if (!t) {
        lerror("not a thread");
        R(0) = -1;
        return;
    }

    linfo("thread %d has priority %#x", t->id, t->priority);

    R(0) = 0;
    R(1) = t->priority;
}

DECL_SVC(SetThreadPriority) {
    KThread* t = HANDLE_GET_TYPED(R(0), KOT_THREAD);
    if (!t) {
        lerror("not a thread");
        R(0) = -1;
        return;
    }

    t->priority = R(1);

    if (t->state == THRD_READY) {
        t->next->prev = t->prev;
        t->prev->next = t->next;
        // need this so thread ready knows this wasnt already in readylist
        t->state = THRD_SLEEP;
        t->next = t->prev = nullptr;
        thread_ready(s, t);
    }

    linfo("thread %d has priority %#x", t->id, t->priority);

    thread_reschedule(s);

    R(0) = 0;
}

DECL_SVC(CreateMutex) {
    MAKE_HANDLE(handle);

    KMutex* mtx = mutex_create();
    if (R(1)) {
        mtx->locker_thrd = caller;
        klist_insert(&caller->owned_mutexes, &mtx->hdr);
    }
    mtx->hdr.refcount = 1;
    HANDLE_SET(handle, mtx);

    linfo("created mutex with handle %x", handle);

    R(0) = 0;
    R(1) = handle;
}

DECL_SVC(ReleaseMutex) {
    KMutex* m = HANDLE_GET_TYPED(R(0), KOT_MUTEX);
    if (!m) {
        lerror("not a mutex");
        R(0) = -1;
        return;
    }

    linfo("releasing mutex %x", R(0));

    mutex_release(s, m);

    R(0) = 0;
}

DECL_SVC(CreateSemaphore) {
    MAKE_HANDLE(handle);

    KSemaphore* sem = semaphore_create(R(1), R(2));
    sem->hdr.refcount = 1;
    HANDLE_SET(handle, sem);

    linfo("created semaphore with handle %x, init=%d, max=%d", handle, R(1),
          R(2));

    R(0) = 0;
    R(1) = handle;
}

DECL_SVC(ReleaseSemaphore) {
    KSemaphore* sem = HANDLE_GET_TYPED(R(1), KOT_SEMAPHORE);
    if (!sem) {
        lerror("not a semaphore");
        R(0) = -1;
        return;
    }

    R(0) = 0;
    R(1) = sem->count;

    linfo("releasing semaphore %x with count %d", R(0), R(2));
    semaphore_release(s, sem, R(2));
}

DECL_SVC(CreateEvent) {
    MAKE_HANDLE(handle);

    KEvent* event = event_create(R(1) == RESET_STICKY);
    event->hdr.refcount = 1;
    HANDLE_SET(handle, event);

    linfo("created event with handle %x, sticky=%d", handle,
          R(1) == RESET_STICKY);

    R(0) = 0;
    R(1) = handle;
}

DECL_SVC(SignalEvent) {
    KEvent* e = HANDLE_GET_TYPED(R(0), KOT_EVENT);
    if (!e) {
        lerror("not an event");
        R(0) = -1;
        return;
    }

    linfo("signaling event %x", R(0));
    event_signal(s, e);

    R(0) = 0;
}

DECL_SVC(ClearEvent) {
    KEvent* e = HANDLE_GET_TYPED(R(0), KOT_EVENT);
    if (!e) {
        lerror("not an event");
        R(0) = -1;
        return;
    }
    e->signal = false;
    R(0) = 0;
}

DECL_SVC(CreateTimer) {
    MAKE_HANDLE(handle);

    KTimer* timer = timer_create_(R(1) == RESET_STICKY, R(1) == RESET_PULSE);
    timer->hdr.refcount = 1;
    HANDLE_SET(handle, timer);

    linfo("created timer with handle %x, resettype=%d", handle, R(1));

    R(0) = 0;
    R(1) = handle;
}

DECL_SVC(SetTimer) {
    KTimer* t = HANDLE_GET_TYPED(R(0), KOT_TIMER);
    if (!t) {
        lerror("not a timer");
        R(0) = -1;
        return;
    }

    s64 delay = (u64) R(2) | (u64) R(3) << 32;
    s64 interval = (u64) R(1) | (u64) R(4) << 32;

    linfo("scheduling timer %x with delay=%d, interval=%d", R(0), delay,
          interval);

    t->interval = interval;
    add_event(&s->sched, (SchedulerCallback) timer_signal, t, delay);

    R(0) = 0;
}

DECL_SVC(CancelTimer) {
    KTimer* t = HANDLE_GET_TYPED(R(0), KOT_TIMER);
    if (!t) {
        lerror("not a timer");
        R(0) = -1;
        return;
    }

    remove_event(&s->sched, (SchedulerCallback) timer_signal, t);

    R(0) = 0;
}

DECL_SVC(ClearTimer) {
    KTimer* t = HANDLE_GET_TYPED(R(0), KOT_TIMER);
    if (!t) {
        lerror("not a timer");
        R(0) = -1;
        return;
    }

    t->signal = false;

    R(0) = 0;
}

DECL_SVC(CreateMemoryBlock) {
    MAKE_HANDLE(handle);

    u32 addr = R(1);
    u32 size = R(2);
    [[maybe_unused]] u32 perm = R(3);

    KSharedMem* shm = calloc(1, sizeof *shm);
    shm->hdr.type = KOT_SHAREDMEM;
    shm->mapaddr = addr;
    shm->size = size;
    shm->hdr.refcount = 1;
    HANDLE_SET(handle, shm);

    linfo("created memory block with handle %x at addr %x", handle, addr);

    R(0) = 0;
    R(1) = handle;
}

DECL_SVC(MapMemoryBlock) {
    u32 memblock = R(0);
    u32 addr = R(1);
    u32 perm = R(2);

    KSharedMem* shmem = HANDLE_GET_TYPED(memblock, KOT_SHAREDMEM);

    if (!shmem) {
        R(0) = -1;
        lerror("invalid memory block handle");
        return;
    }

    if (!addr) addr = shmem->mapaddr;

    if (!shmem->paddr) {
        lerror("mapping unallocated sharedmem");
        R(0) = -1;
        return;
    }

    linfo("mapping shared mem block %x of size %x at %08x", memblock,
          shmem->size, addr);

    memory_virtmap(s, shmem->paddr, addr, shmem->size, perm, MEMST_SHARED);

    R(0) = 0;
}

DECL_SVC(UnmapMemoryBlock) {
    // stub
    R(0) = 0;
}

DECL_SVC(CreateAddressArbiter) {
    MAKE_HANDLE(h);

    KArbiter* arbiter = calloc(1, sizeof *arbiter);
    arbiter->hdr.type = KOT_ARBITER;
    arbiter->hdr.refcount = 1;
    linfo("handle=%x", h);

    HANDLE_SET(h, arbiter);

    R(0) = 0;
    R(1) = h;
}

DECL_SVC(ArbitrateAddress) {
    u32 h = R(0);
    u32 addr = R(1);
    u32 type = R(2);
    s32 value = R(3);
    s64 timeout = R(4) | (u64) R(5) << 32;

    KArbiter* arbiter = HANDLE_GET_TYPED(h, KOT_ARBITER);
    if (!arbiter) {
        lerror("not an arbiter");
        R(0) = -1;
        return;
    }

    R(0) = 0;

    switch (type) {
        case ARBITRATE_SIGNAL:
            linfo("signaling address %08x", addr);
            if (value < 0) {
                KListNode** cur = &arbiter->waiting_thrds;
                while (*cur) {
                    KThread* t = (KThread*) (*cur)->key;
                    if (t->waiting_addr == addr) {
                        thread_wakeup(s, t, &arbiter->hdr);
                        klist_remove(cur);
                    } else {
                        cur = &(*cur)->next;
                    }
                }
            } else {
                for (int i = 0; i < value; i++) {
                    u32 maxprio = THRD_MAX_PRIO;
                    KListNode** toRemove = nullptr;
                    KListNode** cur = &arbiter->waiting_thrds;
                    while (*cur) {
                        KThread* t = (KThread*) (*cur)->key;
                        if (t->waiting_addr == addr) {
                            if (t->priority < maxprio) {
                                maxprio = t->priority;
                                toRemove = cur;
                            }
                        }
                        cur = &(*cur)->next;
                    }
                    if (!toRemove) break;
                    KThread* t = (KThread*) (*toRemove)->key;
                    thread_wakeup(s, t, &arbiter->hdr);
                    klist_remove(toRemove);
                }
            }
            break;
        case ARBITRATE_WAIT:
        case ARBITRATE_DEC_WAIT:
        case ARBITRATE_WAIT_TIMEOUT:
        case ARBITRATE_DEC_WAIT_TIMEOUT:
            if (*(s32*) PTR(addr) < value) {
                klist_insert(&arbiter->waiting_thrds, &caller->hdr);
                klist_insert(&caller->waiting_objs, &arbiter->hdr);
                caller->waiting_addr = addr;
                linfo("waiting on address %08x", addr);
                caller->wait_any = false;
                if (type == ARBITRATE_WAIT_TIMEOUT ||
                    type == ARBITRATE_DEC_WAIT_TIMEOUT) {
                    thread_sleep(s, caller, timeout);
                } else {
                    thread_sleep(s, caller, -1);
                }
            }
            if (type == ARBITRATE_DEC_WAIT ||
                type == ARBITRATE_DEC_WAIT_TIMEOUT) {
                *(s32*) PTR(addr) -= 1;
            }
            break;
        default:
            R(0) = -1;
            lerror("unknown arbitration type");
    }
}

DECL_SVC(CloseHandle) {
    u32 handle = R(0);
    KObject* obj = HANDLE_GET(handle);
    if (!obj) {
        lerror("invalid handle");
        R(0) = 0;
        return;
    }
    HANDLE_SET(handle, nullptr);
    R(0) = 0;
    if (!--obj->refcount) {
        linfo("destroying object of handle %x", handle);
        kobject_destroy(s, obj);
    }
}

DECL_SVC(WaitSynchronization1) {
    u32 handle = R(0);

    s64 timeout = R(2) | (u64) R(3) << 32;

    KObject* obj = HANDLE_GET(handle);
    if (!obj) {
        lerror("invalid handle");
        R(0) = -1;
        return;
    }

    R(0) = 0;

    if (sync_wait(s, caller, obj)) {
        linfo("waiting on handle %x", handle);
        klist_insert(&caller->waiting_objs, obj);
        caller->wait_any = false;
        thread_sleep(s, caller, timeout);
    } else {
        linfo("did not need to wait for handle %x", handle);
    }
}

DECL_SVC(WaitSynchronizationN) {
    u32* handles = PTR(R(1));
    int count = R(2);
    bool waitAll = R(3);
    s64 timeout = R(0) | (u64) R(4) << 32;

    bool wokeup = false;
    int wokeupi = 0;

    R(0) = 0;

    for (int i = 0; i < count; i++) {
        KObject* obj = HANDLE_GET(handles[i]);
        if (!obj) {
            lerror("invalid handle %x", handles[i]);
            continue;
        }
        if (sync_wait(s, caller, obj)) {
            linfo("waiting on handle %x", handles[i]);
            klist_insert(&caller->waiting_objs, obj);
            caller->waiting_objs->val = i;
        } else {
            linfo("handle %x already waited", handles[i]);
            wokeup = true;
            wokeupi = i;
        }
    }

    if ((!waitAll && wokeup) || !caller->waiting_objs) {
        linfo("did not need to wait");
        KListNode** cur = &caller->waiting_objs;
        while (*cur) {
            sync_cancel(caller, (*cur)->key);
            klist_remove(cur);
        }
        R(1) = wokeupi;
    } else {
        linfo("waiting on %d handles", count);
        caller->wait_any = !waitAll;
        thread_sleep(s, caller, timeout);
    }
}

DECL_SVC(DuplicateHandle) {
    KObject* o = HANDLE_GET(R(1));
    if (!o) {
        lerror("invalid handle");
        R(0) = -1;
        return;
    }
    MAKE_HANDLE(dup);
    o->refcount++;
    HANDLE_SET(dup, o);

    R(0) = 0;
    R(1) = dup;
}

DECL_SVC(GetSystemTick) {
    R(0) = s->sched.now;
    R(1) = s->sched.now >> 32;
    s->sched.now += 200; // make time advance so the next read happens later
}

DECL_SVC(GetHandleInfo) {
    u32 handle = R(1);
    u32 type = R(2);

    R(0) = 0;
    switch (type) {
        case 0:
            // this is the ticks since process creation, which for us is
            // just total ticks
            if (handle != 0xffff8001) {
                lwarn("not the process handle");
            }
            R(1) = s->sched.now;
            R(2) = s->sched.now >> 32;
            break;
        default:
            lerror("unknown handle info type %d", type);
            R(0) = -1;
            break;
    }
}

DECL_SVC(GetSystemInfo) {
    u32 type = R(1);
    u32 param = R(2);

    R(0) = 0;
    switch (type) {
        case 0:
            switch (param) {
                case 1:
                    R(1) = s->process.used_memory;
                    R(2) = 0;
                    break;
                default:
                    R(0) = -1;
                    lerror("unknown param");
            }
            break;
        default:
            R(0) = -1;
            lerror("unknown system info type 0x%x", type);
            break;
    }
}

DECL_SVC(GetProcessInfo) {
    u32 type = R(2);

    R(0) = 0;
    switch (type) {
        case 0x01: // ???
            R(1) = sizeof s->process.handles;
            R(2) = 0;
            break;
        case 0x02:
            R(1) = s->process.used_memory;
            R(2) = 0;
            break;
        case 0x14: // linear memory address conversion
            R(1) = FCRAM_PBASE - LINEAR_HEAP_BASE;
            R(2) = 0;
            break;
        default:
            R(0) = -1;
            lerror("unknown process info 0x%x", type);
            break;
    }
}

DECL_SVC(ConnectToPort) {
    MAKE_HANDLE(h);
    char* port = PTR(R(1));
    R(0) = 0;
    if (!strcmp(port, "srv:")) {
        linfo("connected to port '%s' with handle %x", port, h);
        KSession* session = session_create(port_handle_srv);
        session->hdr.refcount = 1;
        HANDLE_SET(h, session);
        R(1) = h;
    } else if (!strcmp(port, "err:f")) {
        linfo("connected to port '%s' with handle %x", port, h);
        KSession* session = session_create(port_handle_errf);
        session->hdr.refcount = 1;
        HANDLE_SET(h, session);
        R(1) = h;
    } else {
        R(0) = -1;
        lerror("unknown port '%s'", port);
    }
}

DECL_SVC(SendSyncRequest) {
    KSession* session = HANDLE_GET_TYPED(R(0), KOT_SESSION);
    if (!session) {
        lerror("invalid session handle %x", R(0));
        R(0) = 0;
        return;
    }
    R(0) = 0;

    // certain sync requests take a nontrivial amount of time
    // and we need to emulate this because games have race conditions relying
    // on this, mainly for FS services
    // thie delay variable can be written by a handler to let us know it should
    // take more time
    u64 delay = 0;

    u32 cmd_addr = caller->tls + IPC_CMD_OFF;
    IPCHeader cmd = *(IPCHeader*) PTR(cmd_addr);
    session->handler(s, cmd, cmd_addr, &delay, session->arg);

    if (delay != 0) {
        // if this was supposed to take time, block the thread until its "done"
        thread_sleep(s, caller, delay);
    }
}

DECL_SVC(GetProcessId) {
    // rn we only emulate one process, also this only seems
    // be used for errdisp anyway
    R(0) = 0;
    R(1) = 0;
}

DECL_SVC(GetThreadId) {
    KThread* t = HANDLE_GET_TYPED(R(1), KOT_THREAD);
    if (!t) {
        lerror("not a thread");
        R(0) = -1;
    }

    linfo("handle %x thread %d", R(1), t->id);

    R(0) = 0;
    R(1) = t->id;
}

DECL_SVC(GetResourceLimit) {
    MAKE_HANDLE(h);
    R(0) = 0;
    KObject* dummy = malloc(sizeof *dummy);
    dummy->type = KOT_RESLIMIT;
    dummy->refcount = 1;
    HANDLE_SET(h, dummy);
    R(1) = h;
}

DECL_SVC(GetResourceLimitLimitValues) {
    s64* values = PTR(R(0));
    u32* names = PTR(R(2));
    s32 count = R(3);
    R(0) = 0;
    for (int i = 0; i < count; i++) {
        switch (names[i]) {
            case RES_MEMORY:
                values[i] = FCRAMUSERSIZE;
                linfo("memory: %08x", values[i]);
                break;
            default:
                lwarn("unknown resource %d", names[i]);
                R(0) = -1;
        }
    }
}

DECL_SVC(GetResourceLimitCurrentValues) {
    s64* values = PTR(R(0));
    u32* names = PTR(R(2));
    s32 count = R(3);
    R(0) = 0;
    for (int i = 0; i < count; i++) {
        switch (names[i]) {
            case RES_MEMORY:
                values[i] = s->process.used_memory;
                linfo("memory: %08x", values[i]);
                break;
            default:
                lwarn("unknown resource %d", names[i]);
                R(0) = -1;
        }
    }
}

DECL_SVC(GetThreadContext) {
    // supposedly this does nothing in the real kernel too
    R(0) = 0;
}

DECL_SVC(Break) {
    lerror("at %08x (lr=%08x)", s->cpu.pc, s->cpu.lr);
    longjmp(ctremu.exceptionJmp, 1);
}

DECL_SVC(OutputDebugString) {
    printf("%*s\n", R(1), (char*) PTR(R(0)));
}

SVCFunc svc_table[SVC_MAX] = {
#define SVC(num, name) [num] = svc_##name,
#include "svcs.inc"
#undef SVC
};

char* svc_names[SVC_MAX] = {
#define SVC(num, name) [num] = #name,
#include "svcs.inc"
#undef SVC
};