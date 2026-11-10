/* srtf.c
 * Shortest Remaining Time First (SRTF) - PREEMPTIVE version of SJF.
 *
 * Idea: always run the process with the SHORTEST REMAINING time.
 * If a new process arrives (or comes back from I/O) and its remaining time
 * is SHORTER than the running process's remaining time, the running process
 * is stopped (preempted) and goes back to the ready list.
 *
 * Tie rule (same as SJF):
 *   1) smaller remaining time wins
 *   2) if equal, the earlier arrival wins
 *   3) if still equal, the smaller PID wins
 *
 * We preempt ONLY if the new one is STRICTLY shorter. If it is equal we do
 * not preempt, because every switch costs time and gains nothing.
 */
#include <stdio.h>
#include "scheduler.h"

/* The ready list: a simple linked list. head points to the first process. */
static Process *ready_head = NULL;

/* Reset the policy (empty the ready list). */
static void srtf_init(void)
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
static void srtf_on_arrival(Process *p)
{
    add_to_ready(p);
}

/* A process is ready again (back from I/O, or it was preempted). */
static void srtf_on_unblock(Process *p)
{
    add_to_ready(p);
}

/* Returns 1 if process a should run BEFORE process b, otherwise 0. */
static int is_better(Process *a, Process *b)
{
    if (a->remaining != b->remaining)
        return a->remaining < b->remaining;   /* shorter remaining wins */
    if (a->arrival != b->arrival)
        return a->arrival < b->arrival;       /* earlier arrival wins */
    return a->pid < b->pid;                   /* smaller PID wins */
}

/* Choose the next process, REMOVE it from the ready list, and return it.
 * Returns NULL if nobody is waiting (CPU stays idle). */
static Process *srtf_pick_next(void)
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
 * Yes, if some waiting process has a STRICTLY shorter remaining time.
 * (This only LOOKS at the list, it does not remove anything.) */
static int srtf_should_preempt(Process *running)
{
    Process *cur = ready_head;

    while (cur != NULL) {
        if (cur->remaining < running->remaining)
            return 1;
        cur = cur->next;
    }
    return 0;
}

/* A process finished. SRTF has nothing to clean up. */
static void srtf_on_exit(Process *p)
{
    (void)p;   /* not used */
}

/* The engine uses this variable to run SRTF. */
Scheduler srtf_scheduler = {
    "SRTF",
    srtf_init,
    srtf_on_arrival,
    srtf_on_unblock,
    srtf_pick_next,
    srtf_should_preempt,
    srtf_on_exit
};
