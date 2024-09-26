#include "svc.h"

#include <stdlib.h>
#include <string.h>

#include "srv.h"
#include "svc_types.h"
#include "thread.h"

void hle3ds_handle_svc(HLE3DS* s, u32 num) {
    num &= 0xff;
    if (!svc_table[num]) {
        lerror("unknown svc 0x%x (0x%x,0x%x,0x%x,0x%x) at %08x (lr=%08x)", num,
               R(0), R(1), R(2), R(3), s->cpu.pc, s->cpu.lr);
        return;
    }
    linfo("svc 0x%x: %s(0x%x,0x%x,0x%x,0x%x,0x%x)", num, svc_names[num], R(0),
          R(1), R(2), R(3), R(4));
    svc_table[num](s);
}

DECL_SVC(ControlMemory) {
    u32 memop = R(0) & MEMOP_MASK;
    u32 memreg = R(0) & MEMOP_REGMASK;
    bool linear = R(0) & MEMOP_LINEAR;
    u32 addr0 = R(1);
    u32 addr1 = R(2);
    u32 size = R(3);
    u32 perm = R(4);

    if (linear && !addr0) addr0 = LINEAR_HEAP_BASE;

    R(0) = 0;
    switch (memop) {
        case MEMOP_ALLOC:
            hle3ds_vmmap(s, addr0, size, perm, MEMST_PRIVATE, linear);
            R(1) = addr0;
            break;
        default:
            lwarn("unknown memory op %d", memop);
            R(0) = -1;
    }
}

DECL_SVC(QueryMemory) {
    u32 addr = R(2);
    VMBlock* b = hle3ds_vmquery(s, addr);
    R(0) = 0;
    R(1) = b->startpg << 12;
    R(2) = (b->endpg - b->startpg) << 12;
    R(3) = b->perm;
    R(4) = b->state;
    R(5) = 0;
}

DECL_SVC(CreateThread) {
    s32 priority = R(0);
    u32 entrypoint = R(1);
    u32 arg = R(2);
    u32 stacktop = R(3);

    if (priority < 0 || priority >= THRD_MAX_PRIO) {
        R(0) = -1;
        return;
    }
    stacktop &= ~7;

    u32 newtid = thread_create(s, entrypoint, stacktop, priority, arg);
    if (newtid == -1) {
        R(0) = -1;
        lerror("ran out of threads");
        return;
    }

    R(0) = 0;
    R(1) = MAKE_HANDLE(HANDLE_THREAD, newtid);

    if (priority > CUR_THREAD.priority) thread_reschedule(s);
}

DECL_SVC(CreateEvent) {
    u32 id = syncobj_create(s, SYNCOBJ_EVENT);
    if (id == -1) {
        R(0) = -1;
        lerror("could not create event");
        return;
    }

    R(0) = 0;
    R(1) = MAKE_HANDLE(HANDLE_SYNCOBJ, id);
}

DECL_SVC(ClearEvent) {
    R(0) = 0;
}

DECL_SVC(MapMemoryBlock) {
    u32 memblock = R(0);
    u32 addr = R(1);
    u32 perm = R(2);

    if (HANDLE_TYPE(memblock) != HANDLE_MEMBLOCK ||
        HANDLE_VAL(memblock) >= s->kernel.shmemblocks.size) {
        R(0) = -1;
        lerror("invalid memory block");
        return;
    }

    s->kernel.shmemblocks.d[HANDLE_VAL(memblock)].vaddr = addr;

    hle3ds_vmmap(s, addr, PAGE_SIZE, perm, MEMST_SHARED, false);
}

DECL_SVC(CreateAddressArbiter) {
    R(0) = 0;
    R(1) = 1;
}

DECL_SVC(ArbitrateAddress) {
    u32 addr = R(1);
    u32 type = R(2);
    s32 value = R(3);
    s64 time = RR(4);

    R(0) = 0;

    switch (type) {
        case ARBITRATE_SIGNAL:
            linfo("signaling address %08x", addr);
            if (value < 0) {
                Vec_foreach(t, s->kernel.addr_arbiter_thrds) {
                    if (t->addr != addr) continue;
                    linfo("waking up thread %d", t->tid);
                    s->kernel.threads.d[t->tid].state = THRD_READY;
                    Vec_remove(s->kernel.addr_arbiter_thrds,
                               t - s->kernel.addr_arbiter_thrds.d);
                    t--;
                }
            } else {
                for (int i = 0; i < value; i++) {
                    u32 maxprio = 0;
                    u32 maxi = -1;
                    u32 tid;
                    Vec_foreach(t, s->kernel.addr_arbiter_thrds) {
                        if (t->addr != addr) continue;
                        if (s->kernel.threads.d[t->tid].priority >= maxprio) {
                            maxprio = s->kernel.threads.d[t->tid].priority;
                            maxi = t - s->kernel.addr_arbiter_thrds.d;
                            tid = t->tid;
                        }
                    }
                    if (maxi == -1) break;
                    linfo("waking up thread %d", tid);
                    s->kernel.threads.d[tid].state = THRD_READY;
                    Vec_remove(s->kernel.addr_arbiter_thrds, maxi);
                }
            }
            thread_reschedule(s);
            break;
        case ARBITRATE_DEC_WAIT:
            *(s32*) PTR(addr) -= 1;
            __attribute__((fallthrough));
        case ARBITRATE_WAIT:
            if (*(s32*) PTR(addr) < value) {
                Vec_push(s->kernel.addr_arbiter_thrds,
                         ((AddressThread){addr, s->kernel.cur_tid}));
                CUR_THREAD.state = THRD_SLEEP;
                linfo("waiting on address %08x", addr);
                thread_reschedule(s);
            }
            break;
        default:
            R(0) = -1;
            lwarn("unknown arbitration type");
    }
}

DECL_SVC(CloseHandle) {
    R(0) = 0;
}

DECL_SVC(WaitSynchronization1) {
    u32 handle = R(0);
    if (HANDLE_TYPE(handle) != HANDLE_SYNCOBJ ||
        HANDLE_VAL(handle) >= s->kernel.syncobjs.size) {
        R(0) = -1;
        lerror("invalid handle %x", handle);
    }
    syncobj_wait(s, HANDLE_VAL(handle));
    R(0) = 0;
}

DECL_SVC(GetSystemTick) {
    RR(0) = s->sched.now;
}

DECL_SVC(ConnectToPort) {
    char* port = PTR(R(1));
    if (!strcmp(port, "srv:")) {
        linfo("connected to port '%s'", port);
        R(0) = 0;
        R(1) = MAKE_HANDLE(HANDLE_SESSION, SRV_SRV);
    } else {
        R(0) = -1;
        lerror("unknown port '%s'", port);
    }
}

DECL_SVC(SendSyncRequest) {
    if (HANDLE_TYPE(R(0)) != HANDLE_SESSION || HANDLE_VAL(R(0)) >= SRV_MAX) {
        lerror("invalid session handle");
        R(0) = -1;
        return;
    }
    u32 cmd_addr = CUR_TLS + IPC_CMD_OFF;
    IPCHeader cmd = {*(u32*) PTR(cmd_addr)};
    services_handle_request(s, HANDLE_VAL(R(0)), cmd, CUR_TLS + IPC_CMD_OFF);
    R(0) = 0;
}

DECL_SVC(GetResourceLimit) {
    R(0) = 0;
    R(1) = 1;
}

DECL_SVC(GetResourceLimitLimitValues) {
    s64* values = PTR(R(0));
    u32* names = PTR(R(2));
    s32 count = R(3);
    R(0) = 0;
    for (int i = 0; i < count; i++) {
        switch (names[i]) {
            case RES_MEMORY:
                values[i] = FCRAMSIZE;
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
                values[i] = s->kernel.used_memory;
                linfo("memory: %08x", values[i]);
                break;
            default:
                lwarn("unknown resource %d", names[i]);
                R(0) = -1;
        }
    }
}

DECL_SVC(Break) {
    lerror("at %08x (lr=%08x)", s->cpu.pc, s->cpu.lr);
    exit(1);
}

DECL_SVC(OutputDebugString) {
    printf("%*s\n", R(1), (char*) PTR(R(0)));
}

SVCFunc svc_table[SVC_MAX] = {
    [0x01] = svc_ControlMemory,
    [0x02] = svc_QueryMemory,
    [0x08] = svc_CreateThread,
    [0x17] = svc_CreateEvent,
    [0x19] = svc_ClearEvent,
    [0x1f] = svc_MapMemoryBlock,
    [0x21] = svc_CreateAddressArbiter,
    [0x22] = svc_ArbitrateAddress,
    [0x23] = svc_CloseHandle,
    [0x24] = svc_WaitSynchronization1,
    [0x28] = svc_GetSystemTick,
    [0x2d] = svc_ConnectToPort,
    [0x32] = svc_SendSyncRequest,
    [0x38] = svc_GetResourceLimit,
    [0x39] = svc_GetResourceLimitLimitValues,
    [0x3a] = svc_GetResourceLimitCurrentValues,
    [0x3c] = svc_Break,
    [0x3d] = svc_OutputDebugString,
};

char* svc_names[SVC_MAX] = {
    [0x01] = "ControlMemory",
    [0x02] = "QueryMemory",
    [0x08] = "CreateThread",
    [0x17] = "CreateEvent",
    [0x19] = "ClearEvent",
    [0x1f] = "MapMemoryBlock",
    [0x21] = "CreateAddressArbiter",
    [0x22] = "ArbitrateAddress",
    [0x23] = "CloseHandle",
    [0x24] = "WaitSynchronization1",
    [0x28] = "GetSystemTick",
    [0x2d] = "ConnectToPort",
    [0x32] = "SendSyncRequest",
    [0x38] = "GetResourceLimit",
    [0x39] = "GetResourceLimitLimitValues",
    [0x3a] = "GetResourceLimitCurrentValues",
    [0x3c] = "Break",
    [0x3d] = "OutputDebugString",
};