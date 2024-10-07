#include "scheduler.h"

#include <stdio.h>

#include "3ds.h"
#include "svc.h"
#include "thread.h"

void run_to_present(Scheduler* sched) {
    u64 end_time = sched->now;
    while (sched->event_queue.size &&
           FIFO_peek(sched->event_queue).time <= end_time) {
        run_next_event(sched);
        if (sched->now > end_time) end_time = sched->now;
    }
    sched->now = end_time;
}

int run_next_event(Scheduler* sched) {
    if (sched->event_queue.size == 0) return 0;

    SchedulerEvent e;
    FIFO_pop(sched->event_queue, e);
    sched->now = e.time;

    e.handler(sched->master, e.arg);

    return sched->now - e.time;
}

void add_event(Scheduler* sched, SchedEventHandler f, u32 event_arg,
               s64 reltime) {
    if (sched->event_queue.size == EVENT_MAX) {
        lerror("event queue is full");
        return;
    }

    FIFO_push(sched->event_queue,
              ((SchedulerEvent){.handler = f,
                                .time = sched->now + reltime,
                                .arg = event_arg}));

    u32 i = (sched->event_queue.tail - 1) % EVENT_MAX;
    while (i != sched->event_queue.head &&
           sched->event_queue.d[i].time <
               sched->event_queue.d[(i - 1) % EVENT_MAX].time) {
        SchedulerEvent tmp = sched->event_queue.d[(i - 1) % EVENT_MAX];
        sched->event_queue.d[(i - 1) % EVENT_MAX] = sched->event_queue.d[i];
        sched->event_queue.d[i] = tmp;
        i = (i - 1) % EVENT_MAX;
    }
}

void remove_event(Scheduler* sched, SchedEventHandler f, u32 event_arg) {
    FIFO_foreach(i, sched->event_queue) {
        if (sched->event_queue.d[i].handler == f &&
            sched->event_queue.d[i].arg == event_arg) {
            sched->event_queue.size--;
            sched->event_queue.tail = (sched->event_queue.tail - 1) % EVENT_MAX;
            for (u32 j = i; j != sched->event_queue.tail;
                 j = (j + 1) % EVENT_MAX) {
                sched->event_queue.d[j] =
                    sched->event_queue.d[(j + 1) % EVENT_MAX];
            }
            return;
        }
    }
}

u64 find_event(Scheduler* sched, SchedEventHandler f) {
    FIFO_foreach(i, sched->event_queue) {
        if (sched->event_queue.d[i].handler == f) {
            return sched->event_queue.d[i].time;
        }
    }
    return -1;
}
