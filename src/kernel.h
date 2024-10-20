#ifndef KERNEL_H
#define KERNEL_H

#include "common.h"

#define HANDLE_MAX BIT(10)
#define HANDLE_BASE 0xffff8000

typedef struct _3DS HLE3DS;

typedef enum {
    KOT_PROCESS,
    KOT_THREAD,
    KOT_MUTEX,
    KOT_SEMAPHORE,
    KOT_EVENT,
    KOT_TIMER,
    KOT_SHAREDMEM,
    KOT_ARBITER,
    KOT_SESSION,
    KOT_RESLIMIT,

    KOT_MAX
} KObjType;

typedef struct {
    KObjType type;
    int refcount;
} KObject;

typedef struct _KListNode {
    KObject* key;
    u32 val;
    struct _KListNode* next;
} KListNode;

u32 handle_new(HLE3DS* s);
#define HANDLE_SET(h, o) (s->process.handles[h - HANDLE_BASE] = (KObject*) (o))
#define HANDLE_GET(h)                                                          \
    (((h - HANDLE_BASE) < HANDLE_MAX) ? s->process.handles[h - HANDLE_BASE]    \
                                      : NULL)
#define HANDLE_GET_TYPED(h, t)                                                 \
    (((h - HANDLE_BASE) < HANDLE_MAX && s->process.handles[h - HANDLE_BASE] && \
      s->process.handles[h - HANDLE_BASE]->type == t)                          \
         ? (void*) s->process.handles[h - HANDLE_BASE]                         \
         : NULL)

void klist_insert(KListNode** l, KObject* o);
void klist_remove(KListNode** l);
u32 klist_remove_key(KListNode** l, KObject* o);

void kobject_destroy(KObject* o);

#endif