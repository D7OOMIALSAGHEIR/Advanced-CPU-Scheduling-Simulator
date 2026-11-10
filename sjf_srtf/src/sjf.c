/* sjf.c
 * Shortest Job First (SJF) - NON-preemptive.
 *
 * Idea: when the CPU is free, pick the ready process with the SHORTEST
 * CPU burst. Once it starts, it runs until its burst ends (no preemption).
 *
 * Tie rule (documented, as the test plan requires):
 *   1) smaller burst wins
 *   2) if equal, the earlier arrival wins
 *   3) if still equal, the smaller PID wins
 */
#include <stdio.h>
#include "scheduler.h"

/* The ready list: a simple linked list. head points to the first process. */
static Process *ready_head = NULL;

/* Reset the policy (empty the ready list). */
static void sjf_init(void)
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
static void sjf_on_arrival(Process *p)
{
    add_to_ready(p);
}

/* A process finished I/O -> it is ready again. */
static void sjf_on_unblock(Process *p)
{
    add_to_ready(p);
}

/* Returns 1 if process a should run BEFORE process b, otherwise 0. */
static int is_better(Process *a, Process *b)
{
    if (a->remaining != b->remaining)
        return a->remaining < b->remaining;   /* shorter burst wins */
    if (a->arrival != b->arrival)
        return a->arrival < b->arrival;       /* earlier arrival wins */
    return a->pid < b->pid;                   /* smaller PID wins */
}

/* Choose the next process, REMOVE it from the ready list, and return it.
 * Returns NULL if nobody is waiting (CPU stays idle). */
static Process *sjf_pick_next(void)
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

/* SJF is NON-preemptive: a running process is never stopped. */
static int sjf_should_preempt(Process *running)
{
    (void)running;   /* not used */
    return 0;        /* never preempt */
}

/* A process finished. SJF has nothing to clean up. */
static void sjf_on_exit(Process *p)
{
    (void)p;   /* not used */
}

/* The engine uses this variable to run SJF. */
Scheduler sjf_scheduler = {
    "SJF",
    sjf_init,
    sjf_on_arrival,
    sjf_on_unblock,
    sjf_pick_next,
    sjf_should_preempt,
    sjf_on_exit
};
