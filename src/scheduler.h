#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "common.h"

#define EVENT_MAX BIT(8)

typedef struct _3DS E3DS;

typedef void (*SchedulerCallback)(E3DS*, void*);

typedef struct {
    u64 time;
    SchedulerCallback handler;
    void* arg;
} SchedulerEvent;

typedef struct _3DS E3DS;

typedef struct {
    u64 now;

    E3DS* master;

    FIFO(SchedulerEvent, EVENT_MAX) event_queue;
} Scheduler;

void run_to_present(Scheduler* sched);
int run_next_event(Scheduler* sched);

#define EVENT_PENDING(sched)                                                   \
    (sched).event_queue.size &&                                                \
        (sched).now >= FIFO_peek((sched).event_queue).time

void add_event(Scheduler* sched, SchedulerCallback f, void* event_arg,
               s64 reltime);
void remove_event(Scheduler* sched, SchedulerCallback f, void* event_arg);
u64 find_event(Scheduler* sched, SchedulerCallback f);

void print_scheduled_events(Scheduler* sched);

#endif