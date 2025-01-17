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

            auto cur = &e->waiting_thrds;
            while (*cur) {
                auto t = (KThread*) (*cur)->key;
                klist_remove_key(&t->waiting_objs, &e->hdr);
                klist_remove(cur);
            }
            free(e);
            break;
        }
        case KOT_SESSION:
        case KOT_RESLIMIT:
            free(o);
            break;
        default:
            lwarn("unimpl free of a kobject type %d", o->type);
    }
}