/* test_priority.c
 * A tiny FAKE ENGINE to test Priority scheduling without the real engine.
 * It moves the clock one tick at a time, calls the scheduler functions,
 * prints a Gantt chart, and checks the answers against hand calculations.
 *
 * The fake engine is the same as the one used for the SJF / SRTF and RR
 * tests, so the results can be compared.
 *
 * Simplifications: no I/O, context-switch cost = 0.
 * Reminder: SMALLER priority number = HIGHER priority.
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "scheduler.h"

extern Scheduler priority_scheduler;
extern int priority_set_preemptive(int on);
extern int priority_is_preemptive(void);

#define MAX_TIME 200

/* Filled by run(): the Gantt chart as text, and the number of preemptions. */
static char last_gantt[MAX_TIME + 1];
static int  last_preemptions;

/* Fill a process with starting values. */
static void make_process(Process *p, int pid, int arrival, int burst,
                         int priority)
{
    p->pid = pid;
    p->arrival = arrival;
    p->priority = priority;
    p->burst = burst;
    p->remaining = burst;
    p->first_run = -1;
    p->finish = -1;
    p->next = NULL;
}

/* Run the fake engine. Prints the Gantt chart and the metrics. */
static void run(Scheduler *s, Process procs[], int n)
{
    char gantt[MAX_TIME];      /* who ran at each tick ('.' = idle) */
    Process *running = NULL;
    int done = 0;
    int time = 0;
    int switches = 0;          /* how many times we preempted */

    s->init();

    while (done < n && time < MAX_TIME) {

        /* 1) Tell the scheduler about processes arriving right now. */
        for (int i = 0; i < n; i++) {
            if (procs[i].arrival == time)
                s->on_arrival(&procs[i]);
        }

        /* 2) Ask the policy: should the running process be stopped?
         *    If yes, it goes back to the ready list. */
        if (running != NULL && s->should_preempt(running)) {
            s->on_unblock(running);
            running = NULL;
            switches++;
        }

        /* 3) If the CPU is idle, ask the scheduler who runs next. */
        if (running == NULL) {
            running = s->pick_next();
            if (running != NULL && running->first_run == -1)
                running->first_run = time;
        }

        /* 4) Run for one tick. */
        if (running == NULL) {
            gantt[time] = '.';
        } else {
            gantt[time] = (char)('0' + running->pid);
            running->remaining--;

            /* 5) Did the burst end? */
            if (running->remaining == 0) {
                running->finish = time + 1;
                s->on_exit(running);
                running = NULL;
                done++;
            }
        }
        time++;
    }

    /* Save the Gantt chart and the preemption count for the asserts. */
    memcpy(last_gantt, gantt, (size_t)time);
    last_gantt[time] = '\0';
    last_preemptions = switches;

    /* Print results. */
    printf("Policy: %s (%s)\n", s->name,
           priority_is_preemptive() ? "preemptive" : "non-preemptive");
    printf("Gantt : %s   (. = idle)\n", last_gantt);

    double sum_wait = 0, sum_tat = 0, sum_resp = 0;
    printf("PID  arrival prio burst  start finish  wait  turnaround response\n");
    for (int i = 0; i < n; i++) {
        int turnaround = procs[i].finish - procs[i].arrival;
        int wait = turnaround - procs[i].burst;
        int response = procs[i].first_run - procs[i].arrival;
        sum_wait += wait;
        sum_tat += turnaround;
        sum_resp += response;
        printf("P%-3d %5d %5d %5d %6d %6d %5d %10d %8d\n",
               procs[i].pid, procs[i].arrival, procs[i].priority,
               procs[i].burst, procs[i].first_run, procs[i].finish,
               wait, turnaround, response);
    }
    printf("Average waiting = %.2f, average turnaround = %.2f, "
           "average response = %.2f, preemptions = %d\n\n",
           sum_wait / n, sum_tat / n, sum_resp / n, switches);
}

/* Helper: waiting time of one process. */
static int wait_of(Process *p)
{
    return p->finish - p->arrival - p->burst;
}

/* Helper: response time of one process. */
static int response_of(Process *p)
{
    return p->first_run - p->arrival;
}

/* The workload used by tests 1 and 2 (same trace for both modes).
 * P1(arr 0, burst 5, prio 3) P2(arr 1, burst 3, prio 1)
 * P3(arr 2, burst 1, prio 4) P4(arr 3, burst 2, prio 2) */
static void make_main_workload(Process p[4])
{
    make_process(&p[0], 1, 0, 5, 3);
    make_process(&p[1], 2, 1, 3, 1);
    make_process(&p[2], 3, 2, 1, 4);
    make_process(&p[3], 4, 3, 2, 2);
}

/* ===================== PRIORITY TESTS ===================== */

/* TEST 1: normal case, PREEMPTIVE, calculated by hand.
 *   time 0   : only P1 -> P1 runs
 *   time 1   : P2 (prio 1) arrives, better than P1 (prio 3) -> preempt
 *   time 1-4 : P2 runs and finishes. P3 (prio 4) and P4 (prio 2) arrive
 *              but are worse than P2 -> no preemption
 *   time 4-6 : ready = P1(3), P3(4), P4(2) -> P4 runs and finishes
 *   time 6-10: P1 runs its last 4 ticks
 *   time10-11: P3 (lowest priority) runs last
 *
 * Expected Gantt: 1 222 44 1111 3
 * Finish:  P1=10 P2=4 P3=11 P4=6
 * Waiting: P1=5  P2=0 P3=8  P4=1  -> average 3.5 */
static void test_prio_preemptive(void)
{
    printf("=== PRIORITY TEST 1: normal case, preemptive ===\n");
    Process p[4];
    make_main_workload(p);
    priority_set_preemptive(1);
    run(&priority_scheduler, p, 4);

    assert(strcmp(last_gantt, "12224411113") == 0);
    assert(p[0].finish == 10);
    assert(p[1].finish == 4);
    assert(p[2].finish == 11);
    assert(p[3].finish == 6);
    assert(wait_of(&p[0]) == 5);
    assert(wait_of(&p[1]) == 0);
    assert(wait_of(&p[2]) == 8);
    assert(wait_of(&p[3]) == 1);
    assert(response_of(&p[1]) == 0);   /* P2 runs the tick it arrives */
    assert(last_preemptions == 1);     /* only P1 at time 1 */
}

/* TEST 2: the SAME workload, NON-PREEMPTIVE.
 *   time 0-5 : P1 keeps the CPU even though P2 (prio 1) arrives at 1
 *   time 5-8 : ready = P2(1), P3(4), P4(2) -> P2
 *   time 8-10: P4
 *   time10-11: P3
 *
 * Expected Gantt: 11111 222 44 3
 * Finish:  P1=5 P2=8 P3=11 P4=10
 * Waiting: P1=0 P2=4 P3=8  P4=5  -> average 4.25 (worse than test 1) */
static void test_prio_non_preemptive(void)
{
    printf("=== PRIORITY TEST 2: same workload, non-preemptive ===\n");
    Process p[4];
    make_main_workload(p);
    priority_set_preemptive(0);
    run(&priority_scheduler, p, 4);

    assert(strcmp(last_gantt, "11111222443") == 0);
    assert(p[0].finish == 5);
    assert(p[1].finish == 8);
    assert(p[2].finish == 11);
    assert(p[3].finish == 10);
    assert(wait_of(&p[1]) == 4);       /* the high-priority job had to wait */
    assert(wait_of(&p[3]) == 5);
    assert(last_preemptions == 0);

    priority_set_preemptive(1);        /* back to the default */
}

/* TEST 3: chain of preemptions - each arrival is better than the last.
 * P1(arr 0, burst 4, prio 5) P2(arr 1, burst 3, prio 3) P3(arr 2, burst 2, prio 1)
 *   time 0  : P1          time 1: P2 preempts P1
 *   time 2-4: P3 preempts P2 and finishes
 *   time 4-6: P2 (2 left)  time 6-9: P1 (3 left)
 * Expected Gantt: 1 2 33 22 111, 2 preemptions, every response = 0. */
static void test_prio_chain(void)
{
    printf("=== PRIORITY TEST 3: chain of preemptions ===\n");
    Process p[3];
    make_process(&p[0], 1, 0, 4, 5);
    make_process(&p[1], 2, 1, 3, 3);
    make_process(&p[2], 3, 2, 2, 1);
    run(&priority_scheduler, p, 3);

    assert(strcmp(last_gantt, "123322111") == 0);
    assert(p[0].finish == 9);
    assert(p[1].finish == 6);
    assert(p[2].finish == 4);
    assert(response_of(&p[0]) == 0);
    assert(response_of(&p[1]) == 0);
    assert(response_of(&p[2]) == 0);
    assert(last_preemptions == 2);
}

/* TEST 4: EQUAL priorities (tie rule, rules 2 and 3).
 * P1(arr 0, burst 3, prio 2), then P3 and P2 both arrive at 1 with prio 2.
 * (P3 is listed BEFORE P2 on purpose, so the order in the list is wrong.)
 *   - Equal priority does NOT preempt P1.
 *   - At time 3, P2 and P3 have the same priority and arrival ->
 *     the smaller PID (P2) wins.
 * Expected Gantt: 111 22 3, 0 preemptions. */
static void test_prio_equal_tie(void)
{
    printf("=== PRIORITY TEST 4: equal priorities (tie rule) ===\n");
    Process p[3];
    make_process(&p[0], 1, 0, 3, 2);
    make_process(&p[1], 3, 1, 1, 2);
    make_process(&p[2], 2, 1, 2, 2);
    run(&priority_scheduler, p, 3);

    assert(strcmp(last_gantt, "111223") == 0);
    assert(p[0].finish == 3);
    assert(p[2].finish == 5);          /* P2 */
    assert(p[1].finish == 6);          /* P3 */
    assert(last_preemptions == 0);
}

/* TEST 5: all arrive at time 0 -> pure priority order, priority 0 is top.
 * P1(burst 2, prio 3) P2(burst 3, prio 0) P3(burst 1, prio 1)
 * Expected Gantt: 222 3 11
 * Finish: P1=6 P2=3 P3=4 */
static void test_prio_all_at_zero(void)
{
    printf("=== PRIORITY TEST 5: all arrive at time 0 ===\n");
    Process p[3];
    make_process(&p[0], 1, 0, 2, 3);
    make_process(&p[1], 2, 0, 3, 0);
    make_process(&p[2], 3, 0, 1, 1);
    run(&priority_scheduler, p, 3);

    assert(strcmp(last_gantt, "222311") == 0);
    assert(p[0].finish == 6);
    assert(p[1].finish == 3);
    assert(p[2].finish == 4);
    assert(last_preemptions == 0);
}

/* TEST 6: single process that arrives late (idle gap at the start). */
static void test_prio_idle_gap(void)
{
    printf("=== PRIORITY TEST 6: single process, CPU idle at the start ===\n");
    Process p[1];
    make_process(&p[0], 1, 3, 2, 5);
    run(&priority_scheduler, p, 1);

    assert(strcmp(last_gantt, "...11") == 0);
    assert(p[0].first_run == 3);
    assert(p[0].finish == 5);
    assert(wait_of(&p[0]) == 0);
}

/* TEST 7: idle gap in the MIDDLE, then a higher-priority arrival.
 * P1(arr 0, burst 2, prio 1) P2(arr 4, burst 3, prio 5) P3(arr 5, burst 1, prio 2)
 *   time 0-2: P1    time 2-4: idle    time 4: P2 starts
 *   time 5  : P3 (prio 2) preempts P2 (prio 5)
 * Expected Gantt: 11 .. 2 3 22 */
static void test_prio_idle_in_middle(void)
{
    printf("=== PRIORITY TEST 7: CPU idle in the middle ===\n");
    Process p[3];
    make_process(&p[0], 1, 0, 2, 1);
    make_process(&p[1], 2, 4, 3, 5);
    make_process(&p[2], 3, 5, 1, 2);
    run(&priority_scheduler, p, 3);

    assert(strcmp(last_gantt, "11..2322") == 0);
    assert(p[0].finish == 2);
    assert(p[1].finish == 8);
    assert(p[2].finish == 6);
    assert(last_preemptions == 1);
}

/* TEST 8: STARVATION (the problem workload W2 shows, rule 4).
 * A short low-priority job keeps losing to high-priority jobs that keep
 * arriving. P1(arr 0, burst 2, prio 9) P2(arr 0, burst 3, prio 1)
 * P3(arr 2, burst 3, prio 1) P4(arr 5, burst 3, prio 1)
 * Expected Gantt: 222 333 444 11
 * P1 needs only 2 ticks but waits 9. With more high-priority arrivals it
 * would wait forever. There is no aging here; the Adaptive MLFQ fixes it. */
static void test_prio_starvation(void)
{
    printf("=== PRIORITY TEST 8: starvation of a low-priority job ===\n");
    Process p[4];
    make_process(&p[0], 1, 0, 2, 9);
    make_process(&p[1], 2, 0, 3, 1);
    make_process(&p[2], 3, 2, 3, 1);
    make_process(&p[3], 4, 5, 3, 1);
    run(&priority_scheduler, p, 4);

    assert(strcmp(last_gantt, "22233344411") == 0);
    assert(wait_of(&p[0]) == 9);
    assert(response_of(&p[0]) == 9);
    assert(p[0].priority == 9);        /* fixed: priority never changed */
    assert(last_preemptions == 0);     /* equal priorities never preempt */
}

/* TEST 9: a preempted process keeps its place by ARRIVAL, not by when it
 * re-entered the list. P1 and P2 both have prio 4; P1 arrived first.
 * P1(arr 0, burst 3, prio 4) P2(arr 1, burst 2, prio 4) P3(arr 1, burst 1, prio 0)
 *   time 1: P3 preempts P1. P1 goes to the END of the list, after P2.
 *   time 2: P1 vs P2, same priority -> P1 wins (earlier arrival).
 * Expected Gantt: 1 3 11 22 */
static void test_prio_preempted_keeps_order(void)
{
    printf("=== PRIORITY TEST 9: preempted process keeps its order ===\n");
    Process p[3];
    make_process(&p[0], 1, 0, 3, 4);
    make_process(&p[1], 2, 1, 2, 4);
    make_process(&p[2], 3, 1, 1, 0);
    run(&priority_scheduler, p, 3);

    assert(strcmp(last_gantt, "131122") == 0);
    assert(p[0].finish == 4);
    assert(p[1].finish == 6);
    assert(p[2].finish == 2);
    assert(last_preemptions == 1);
}

/* TEST 10: bad mode values are rejected and the old mode is kept. */
static void test_prio_bad_mode(void)
{
    printf("=== PRIORITY TEST 10: invalid mode is rejected ===\n");
    assert(priority_set_preemptive(0) == 0);
    assert(priority_set_preemptive(2) == -1);
    assert(priority_set_preemptive(-1) == -1);
    assert(priority_is_preemptive() == 0);   /* still the last good value */
    assert(priority_set_preemptive(1) == 0);
    assert(priority_is_preemptive() == 1);
    printf("modes 2 and -1 rejected, default (preemptive) restored\n\n");
}

int main(void)
{
    test_prio_preemptive();
    test_prio_non_preemptive();
    test_prio_chain();
    test_prio_equal_tie();
    test_prio_all_at_zero();
    test_prio_idle_gap();
    test_prio_idle_in_middle();
    test_prio_starvation();
    test_prio_preempted_keeps_order();
    test_prio_bad_mode();
    printf("ALL PRIORITY TESTS PASSED\n");
    return 0;
}
