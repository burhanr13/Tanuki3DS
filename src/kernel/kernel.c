#include "kernel.h"

#include "3ds.h"

u32 handle_new(E3DS* s) {
    for (int i = 0; i < HANDLE_MAX; i++) {
        if (!s->process.handles[i]) {
            return HANDLE_BASE + i;
        }
    }
    lerror("no free handles");
    return 0;
}

void klist_insert(KListNode** l, KObject* o) {
    KListNode* newNode = malloc(sizeof *newNode);
    newNode->key = o;
    newNode->next = *l;
    *l = newNode;
}

void klist_remove(KListNode** l) {
    KListNode* cur = *l;
    *l = cur->next;
    free(cur);
}

u32 klist_remove_key(KListNode** l, KObject* o) {
    while (*l) {
        if ((*l)->key == o) {
            u32 v = (*l)->val;
            klist_remove(l);
            return v;
        }
        l = &(*l)->next;
    }
    return 0;
}

#define FREE_SYNCOBJ(o)                                                        \
    ({                                                                         \
        auto cur = &(o)->waiting_thrds;                                        \
        while (*cur) {                                                         \
            auto t = (KThread*) (*cur)->key;                                   \
            klist_remove_key(&t->waiting_objs, &(o)->hdr);                     \
            klist_remove(cur);                                                 \
        }                                                                      \
    })

void kobject_destroy(E3DS* s, KObject* o) {
    switch (o->type) {
        case KOT_THREAD: {
            auto t = (KThread*) o;
            thread_kill(s, t);
            free(t);
            break;
        }
        case KOT_EVENT: {
            auto e = (KEvent*) o;
            FREE_SYNCOBJ(e);
            free(e);
            break;
        }
        case KOT_MUTEX: {
            auto m = (KMutex*) o;
            FREE_SYNCOBJ(m);
            free(m);
            break;
        }
        case KOT_SEMAPHORE: {
            auto s = (KSemaphore*) o;
            FREE_SYNCOBJ(s);
            free(s);
            break;
        }
        case KOT_ARBITER: {
            auto arb = (KArbiter*) o;
            FREE_SYNCOBJ(arb);
            free(arb);
            break;
        }
        case KOT_SESSION:
        case KOT_RESLIMIT:
        case KOT_SHAREDMEM:
            free(o);
            break;
        default:
            lwarn("unimpl free of a kobject type %d", o->type);
            free(o);
            break;
    }
}