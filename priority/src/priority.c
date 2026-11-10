/* priority.c
 * Fixed-Priority scheduling - PREEMPTIVE by default (as in the proposal),
 * with an option to switch to the NON-PREEMPTIVE version.
 *
 * Idea: every process has a fixed priority number. The process with the
 * HIGHEST priority (= the SMALLEST number) always runs. The priority never
 * changes while the process waits.
 *
 * Preemptive: if a process arrives (or comes back from I/O) with a STRICTLY
 *             higher priority than the running one, the running process is
 *             stopped (preempted) and goes back to the ready list.
 * Non-preemptive: the running process keeps the CPU until its burst ends;
 *             the priority is only used to choose who runs NEXT.
 *
 * Rules (documented, as the test plan requires):
 *   1) Smaller priority number = higher priority (0 is the top).
 *   2) Tie rule: if priorities are equal, the earlier arrival wins;
 *      if still equal, the smaller PID wins (same tie rule as SJF/SRTF).
 *   3) We preempt ONLY if the waiting process is STRICTLY higher priority.
 *      Equal priority never preempts: a switch costs time and gains nothing.
 *   4) NO aging. This is on purpose: fixed priority CAN starve low-priority
 *      processes (workload W2 shows it). Aging and boost belong to the
 *      Adaptive MLFQ, which is the scheduler that fixes this problem.
 *
 * The mode is configurable with priority_set_preemptive(). Default = 1.
 */
#include <stdio.h>
#include "scheduler.h"

/* The ready list: a simple linked list. head points to the first process. */
static Process *ready_head = NULL;

/* 1 = preemptive (default), 0 = non-preemptive. */
static int preemptive = 1;

/* Change the mode. Call it BEFORE the simulation starts.
 * Returns 0 if OK, or -1 if the value is not 0 or 1 (old mode is kept). */
int priority_set_preemptive(int on)
{
    if (on != 0 && on != 1)
        return -1;
    preemptive = on;
    return 0;
}

/* Read the current mode (used by tests and by the results output). */
int priority_is_preemptive(void)
{
    return preemptive;
}

/* Reset the policy (empty the ready list). The mode is NOT reset,
 * because it is a setting, not a part of the queue state. */
static void prio_init(void)
{
    ready_head = NULL;
}

/* Add a process to the END of the ready list. */
static void add_to_ready(Process *p)
{
    p->next = NULL;

    if (ready_head == NULL) {          /* list is empty */
        ready_head = p;
        return;
    }

    Process *cur = ready_head;
    while (cur->next != NULL) {        /* go to the last node */
        cur = cur->next;
    }
    cur->next = p;
}

/* A new process arrived -> put it in the ready list. */
static void prio_on_arrival(Process *p)
{
    add_to_ready(p);
}

/* A process is ready again (back from I/O, or it was preempted).
 * Its priority does not change (fixed priority, rule 4). */
static void prio_on_unblock(Process *p)
{
    add_to_ready(p);
}

/* Returns 1 if process a should run BEFORE process b, otherwise 0. */
static int is_better(Process *a, Process *b)
{
    if (a->priority != b->priority)
        return a->priority < b->priority;     /* higher priority wins */
    if (a->arrival != b->arrival)
        return a->arrival < b->arrival;       /* earlier arrival wins */
    return a->pid < b->pid;                   /* smaller PID wins     */
}

/* Choose the next process, REMOVE it from the ready list, and return it.
 * Returns NULL if nobody is waiting (CPU stays idle). */
static Process *prio_pick_next(void)
{
    if (ready_head == NULL)
        return NULL;

    Process *best = ready_head;     /* best process found so far        */
    Process *best_prev = NULL;      /* the node just before "best"      */
    Process *prev = ready_head;
    Process *cur = ready_head->next;

    /* Look at every process in the list and keep the best one. */
    while (cur != NULL) {
        if (is_better(cur, best)) {
            best = cur;
            best_prev = prev;
        }
        prev = cur;
        cur = cur->next;
    }

    /* Remove "best" from the list. */
    if (best_prev == NULL)
        ready_head = best->next;        /* best was the first node */
    else
        best_prev->next = best->next;   /* skip over best          */

    best->next = NULL;
    return best;
}

/* Should the running process be stopped right now?
 * Only in preemptive mode, and only if some waiting process has a
 * STRICTLY higher priority (smaller number) - rule 3.
 * (This only LOOKS at the list, it does not remove anything.) */
static int prio_should_preempt(Process *running)
{
    if (!preemptive)
        return 0;

    Process *cur = ready_head;

    while (cur != NULL) {
        if (cur->priority < running->priority)
            return 1;
        cur = cur->next;
    }
    return 0;
}

/* A process finished. Priority has nothing to clean up. */
static void prio_on_exit(Process *p)
{
    (void)p;   /* not used */
}

/* The engine uses this variable to run Priority scheduling. */
Scheduler priority_scheduler = {
    "PRIORITY",
    prio_init,
    prio_on_arrival,
    prio_on_unblock,
    prio_pick_next,
    prio_should_preempt,
    prio_on_exit
};
