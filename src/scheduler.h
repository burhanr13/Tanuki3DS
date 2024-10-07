#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "types.h"

#define EVENT_MAX BIT(8)

typedef struct _3DS HLE3DS;

typedef void (*SchedEventHandler)(HLE3DS*, u32);

typedef struct {
    u64 time;
    SchedEventHandler handler;
    u32 arg;
} SchedulerEvent;

typedef struct _3DS HLE3DS;

typedef struct {
    u64 now;

    HLE3DS* master;

    FIFO(SchedulerEvent, EVENT_MAX) event_queue;
} Scheduler;

void run_to_present(Scheduler* sched);
int run_next_event(Scheduler* sched);

#define EVENT_PENDING(sched)                                                   \
    (sched).event_queue.size &&                                                \
        (sched).now >= FIFO_peek((sched).event_queue).time

void add_event(Scheduler* sched, SchedEventHandler f, u32 event_arg,
               s64 reltime);
void remove_event(Scheduler* sched, SchedEventHandler f, u32 event_arg);
u64 find_event(Scheduler* sched, SchedEventHandler f);

void print_scheduled_events(Scheduler* sched);

#endif