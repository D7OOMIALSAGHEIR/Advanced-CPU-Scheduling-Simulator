/* test_schedulers.c
 * A tiny FAKE ENGINE to test SJF and SRTF without the real engine.
 * It moves the clock one tick at a time, calls the scheduler functions,
 * prints a Gantt chart, and checks the answers against hand calculations.
 *
 * Simplifications: no I/O, context-switch cost = 0.
 */
#include <stdio.h>
#include <assert.h>
#include "scheduler.h"

extern Scheduler sjf_scheduler;
extern Scheduler srtf_scheduler;

#define MAX_TIME 200

/* Fill a process with starting values. */
static void make_process(Process *p, int pid, int arrival, int burst)
{
    p->pid = pid;
    p->arrival = arrival;
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

    /* Print results. */
    printf("Policy: %s\n", s->name);
    printf("Gantt : ");
    for (int t = 0; t < time; t++) printf("%c", gantt[t]);
    printf("   (. = idle)\n");

    double sum_wait = 0, sum_tat = 0;
    printf("PID  arrival burst  start finish  wait  turnaround response\n");
    for (int i = 0; i < n; i++) {
        int turnaround = procs[i].finish - procs[i].arrival;
        int wait = turnaround - procs[i].burst;
        int response = procs[i].first_run - procs[i].arrival;
        sum_wait += wait;
        sum_tat += turnaround;
        printf("P%-3d %5d %5d %6d %6d %5d %10d %8d\n",
               procs[i].pid, procs[i].arrival, procs[i].burst,
               procs[i].first_run, procs[i].finish,
               wait, turnaround, response);
    }
    printf("Average waiting = %.2f, average turnaround = %.2f, preemptions = %d\n\n",
           sum_wait / n, sum_tat / n, switches);
}

/* Helper: waiting time of one process. */
static int wait_of(Process *p)
{
    return p->finish - p->arrival - p->burst;
}

/* ===================== SJF TESTS ===================== */

/* TEST 1: normal case, calculated by hand.
 * P1(arr 0, burst 7) P2(arr 2, burst 4) P3(arr 4, burst 1) P4(arr 5, burst 4)
 * Expected Gantt: 1111111 3 2222 4444
 * Waiting: P1=0 P2=6 P3=3 P4=7  -> average 4.0 */
static void test_sjf_normal(void)
{
    printf("=== SJF TEST 1: normal case ===\n");
    Process p[4];
    make_process(&p[0], 1, 0, 7);
    make_process(&p[1], 2, 2, 4);
    make_process(&p[2], 3, 4, 1);
    make_process(&p[3], 4, 5, 4);
    run(&sjf_scheduler, p, 4);

    assert(wait_of(&p[0]) == 0);
    assert(wait_of(&p[1]) == 6);
    assert(wait_of(&p[2]) == 3);
    assert(wait_of(&p[3]) == 7);
    assert(p[2].finish == 8);   /* P3 (shortest) runs right after P1 */
}

/* TEST 2: single process that arrives late (idle gap). */
static void test_sjf_idle_gap(void)
{
    printf("=== SJF TEST 2: single process, CPU idle at the start ===\n");
    Process p[1];
    make_process(&p[0], 1, 3, 2);
    run(&sjf_scheduler, p, 1);

    assert(p[0].first_run == 3);
    assert(p[0].finish == 5);
    assert(wait_of(&p[0]) == 0);
}

/* TEST 3: equal bursts (tie rule).
 * P2 and P3 tie on burst and arrival -> smaller PID (P2) goes first. */
static void test_sjf_tie(void)
{
    printf("=== SJF TEST 3: tie (equal bursts and arrivals) ===\n");
    Process p[3];
    make_process(&p[0], 1, 0, 3);
    make_process(&p[1], 2, 1, 2);
    make_process(&p[2], 3, 1, 2);
    run(&sjf_scheduler, p, 3);

    assert(p[1].finish == 5);   /* P2 first  */
    assert(p[2].finish == 7);   /* P3 second */
}

/* TEST 4: all processes arrive at time 0. */
static void test_sjf_all_at_zero(void)
{
    printf("=== SJF TEST 4: all arrive at time 0 ===\n");
    Process p[3];
    make_process(&p[0], 1, 0, 6);
    make_process(&p[1], 2, 0, 2);
    make_process(&p[2], 3, 0, 4);
    run(&sjf_scheduler, p, 3);

    /* Order should be P2 (2), P3 (4), P1 (6). */
    assert(p[1].finish == 2);
    assert(p[2].finish == 6);
    assert(p[0].finish == 12);
}

/* ===================== SRTF TESTS ===================== */

/* TEST 1: normal case with preemption, calculated by hand.
 * P1(arr 0, burst 8) P2(arr 1, burst 4) P3(arr 2, burst 9) P4(arr 3, burst 5)
 * Expected Gantt: 1 2222 44444 1111111 333333333
 * Finish: P1=17 P2=5 P3=26 P4=10
 * Waiting: P1=9 P2=0 P3=15 P4=2  -> average 6.5 */
static void test_srtf_normal(void)
{
    printf("=== SRTF TEST 1: normal case with preemption ===\n");
    Process p[4];
    make_process(&p[0], 1, 0, 8);
    make_process(&p[1], 2, 1, 4);
    make_process(&p[2], 3, 2, 9);
    make_process(&p[3], 4, 3, 5);
    run(&srtf_scheduler, p, 4);

    assert(p[0].finish == 17);
    assert(p[1].finish == 5);
    assert(p[2].finish == 26);
    assert(p[3].finish == 10);
    assert(wait_of(&p[0]) == 9);
    assert(wait_of(&p[1]) == 0);
    assert(wait_of(&p[2]) == 15);
    assert(wait_of(&p[3]) == 2);
}

/* TEST 2: equal remaining time -> NO preemption.
 * At time 1, P1 has 4 left and P2 arrives with 4. Equal, so P1 keeps going. */
static void test_srtf_no_preempt_on_tie(void)
{
    printf("=== SRTF TEST 2: tie -> no preemption ===\n");
    Process p[2];
    make_process(&p[0], 1, 0, 5);
    make_process(&p[1], 2, 1, 4);
    run(&srtf_scheduler, p, 2);

    assert(p[0].finish == 5);   /* P1 was not interrupted */
    assert(p[1].finish == 9);
}

/* TEST 3: same workload as SJF TEST 1. SRTF should be better.
 * Expected Gantt: 11 22 3 22 4444 11111
 * Finish: P1=16 P2=7 P3=5 P4=11
 * Waiting: P1=9 P2=1 P3=0 P4=2 -> average 3.0  (SJF got 4.0) */
static void test_srtf_vs_sjf(void)
{
    printf("=== SRTF TEST 3: same workload as SJF test 1 ===\n");
    Process p[4];
    make_process(&p[0], 1, 0, 7);
    make_process(&p[1], 2, 2, 4);
    make_process(&p[2], 3, 4, 1);
    make_process(&p[3], 4, 5, 4);
    run(&srtf_scheduler, p, 4);

    assert(p[0].finish == 16);
    assert(p[1].finish == 7);
    assert(p[2].finish == 5);
    assert(p[3].finish == 11);
    assert(wait_of(&p[0]) + wait_of(&p[1]) + wait_of(&p[2]) + wait_of(&p[3]) == 12);
}

/* TEST 4: single process arriving late (idle gap). */
static void test_srtf_idle_gap(void)
{
    printf("=== SRTF TEST 4: single process, CPU idle at the start ===\n");
    Process p[1];
    make_process(&p[0], 1, 3, 2);
    run(&srtf_scheduler, p, 1);

    assert(p[0].first_run == 3);
    assert(p[0].finish == 5);
}

/* TEST 5: all arrive at time 0 -> behaves exactly like SJF. */
static void test_srtf_all_at_zero(void)
{
    printf("=== SRTF TEST 5: all arrive at time 0 ===\n");
    Process p[3];
    make_process(&p[0], 1, 0, 6);
    make_process(&p[1], 2, 0, 2);
    make_process(&p[2], 3, 0, 4);
    run(&srtf_scheduler, p, 3);

    assert(p[1].finish == 2);
    assert(p[2].finish == 6);
    assert(p[0].finish == 12);
}

int main(void)
{
    test_sjf_normal();
    test_sjf_idle_gap();
    test_sjf_tie();
    test_sjf_all_at_zero();
    printf("ALL SJF TESTS PASSED\n\n");

    test_srtf_normal();
    test_srtf_no_preempt_on_tie();
    test_srtf_vs_sjf();
    test_srtf_idle_gap();
    test_srtf_all_at_zero();
    printf("ALL SRTF TESTS PASSED\n");
    return 0;
}
