#include "scheduler.h"

#include <stdio.h>

#include "3ds.h"
#include "kernel/svc.h"
#include "kernel/thread.h"

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

void add_event(Scheduler* sched, SchedulerCallback f, void* event_arg,
               s64 reltime) {
    if (sched->event_queue.size == EVENT_MAX) {
        lerror("event queue is full");
        return;
    }

    FIFO_push(sched->event_queue,
              ((SchedulerEvent) {.handler = f,
                                 .time = sched->now + reltime,
                                 .arg = event_arg}));

    auto i = sched->event_queue.tail - 1wb;
    while (i != sched->event_queue.head &&
           sched->event_queue.d[i].time < sched->event_queue.d[i - 1wb].time) {
        SchedulerEvent tmp = sched->event_queue.d[i - 1wb];
        sched->event_queue.d[i - 1wb] = sched->event_queue.d[i];
        sched->event_queue.d[i] = tmp;
        i--;
    }
}

void remove_event(Scheduler* sched, SchedulerCallback f, void* event_arg) {
    FIFO_foreach(it, sched->event_queue) {
        if (it.p->handler == f && it.p->arg == event_arg) {
            sched->event_queue.size--;
            sched->event_queue.tail--;
            for (auto j = it._i; j != sched->event_queue.tail; j++) {
                sched->event_queue.d[j] = sched->event_queue.d[j + 1wb];
            }
            return;
        }
    }
}

u64 find_event(Scheduler* sched, SchedulerCallback f) {
    FIFO_foreach(it, sched->event_queue) {
        if (it.p->handler == f) {
            return it.p->time;
        }
    }
    return -1;
}
