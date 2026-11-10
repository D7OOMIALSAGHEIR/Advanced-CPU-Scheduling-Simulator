/* rr.c
 * Round Robin (RR) - PREEMPTIVE, with a fixed time quantum.
 *
 * Idea: the ready processes wait in a FIFO queue (first in, first out).
 * The process at the front runs for at most one QUANTUM. If it is not
 * finished when the quantum ends, it is stopped (preempted) and goes to the
 * BACK of the queue, and the next process at the front runs.
 *
 * Rules (documented, as the test plan requires):
 *   1) Order is FIFO: whoever entered the ready queue first runs first.
 *      RR never looks at burst length or priority.
 *   2) If a new process arrives at the SAME tick the quantum ends, the new
 *      process enters the queue BEFORE the preempted one (the usual textbook
 *      rule). The engine gives us this for free, because it reports arrivals
 *      first and asks about preemption second.
 *   3) If the quantum ends and NOBODY is waiting, the running process is not
 *      stopped. It simply gets a fresh quantum. Switching to "nobody" would
 *      cost time and gain nothing.
 *   4) If a process finishes or blocks before its quantum ends, the next
 *      process starts immediately with a full quantum.
 *
 * The quantum is configurable with rr_set_quantum(). Default = 4 ticks.
 */
#include <stdio.h>
#include "scheduler.h"

#define RR_DEFAULT_QUANTUM 4

/* The ready queue: a linked list with a head (front) and a tail (back).
 * We add at the tail and remove from the head, so both are fast. */
static Process *ready_head = NULL;
static Process *ready_tail = NULL;

/* Length of one time slice, in ticks. */
static int quantum = RR_DEFAULT_QUANTUM;

/* The "remaining" value of the running process at the moment it was
 * dispatched. We use it to know how long the process has been running:
 *     ticks used in this turn = slice_start_remaining - p->remaining      */
static int slice_start_remaining = 0;

/* Change the quantum. Call it BEFORE the simulation starts.
 * Returns 0 if OK, or -1 if q is not valid (the old quantum is kept). */
int rr_set_quantum(int q)
{
    if (q <= 0)
        return -1;
    quantum = q;
    return 0;
}

/* Read the current quantum (used by tests and by the results output). */
int rr_get_quantum(void)
{
    return quantum;
}

/* Reset the policy (empty the ready queue). The quantum is NOT reset,
 * because it is a setting, not a part of the queue state. */
static void rr_init(void)
{
    ready_head = NULL;
    ready_tail = NULL;
    slice_start_remaining = 0;
}

/* Add a process to the BACK of the ready queue. */
static void add_to_ready(Process *p)
{
    p->next = NULL;

    if (ready_head == NULL) {          /* queue is empty */
        ready_head = p;
        ready_tail = p;
        return;
    }

    ready_tail->next = p;              /* put it after the last node */
    ready_tail = p;
}

/* A new process arrived -> it goes to the back of the queue. */
static void rr_on_arrival(Process *p)
{
    add_to_ready(p);
}

/* A process is ready again (back from I/O, or its quantum ended).
 * It goes to the back of the queue, like everybody else. */
static void rr_on_unblock(Process *p)
{
    add_to_ready(p);
}

/* Take the process at the FRONT of the queue, REMOVE it, and return it.
 * Returns NULL if nobody is waiting (CPU stays idle). */
static Process *rr_pick_next(void)
{
    if (ready_head == NULL)
        return NULL;

    Process *p = ready_head;

    ready_head = p->next;
    if (ready_head == NULL)            /* the queue is empty now */
        ready_tail = NULL;

    p->next = NULL;

    /* Remember where this turn started, so we can count its ticks. */
    slice_start_remaining = p->remaining;
    return p;
}

/* Should the running process be stopped right now?
 * Yes, if it used up a full quantum AND somebody else is waiting.
 * (This only LOOKS at the state, it does not change anything.)
 *
 * "used % quantum == 0" means a quantum just ended. Because of rule 3 a
 * process may run for several quanta in a row when the queue is empty, so
 * the quantum ends at used = quantum, 2*quantum, 3*quantum, ... */
static int rr_should_preempt(Process *running)
{
    int used = slice_start_remaining - running->remaining;

    if (used <= 0)
        return 0;                      /* it has not run yet in this turn */
    if (used % quantum != 0)
        return 0;                      /* the quantum is not finished     */
    if (ready_head == NULL)
        return 0;                      /* nobody is waiting (rule 3)      */
    return 1;
}

/* A process finished. RR has nothing to clean up. */
static void rr_on_exit(Process *p)
{
    (void)p;   /* not used */
}

/* The engine uses this variable to run Round Robin. */
Scheduler rr_scheduler = {
    "RR",
    rr_init,
    rr_on_arrival,
    rr_on_unblock,
    rr_pick_next,
    rr_should_preempt,
    rr_on_exit
};
