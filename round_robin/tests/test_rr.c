/* test_rr.c
 * A tiny FAKE ENGINE to test Round Robin without the real engine.
 * It moves the clock one tick at a time, calls the scheduler functions,
 * prints a Gantt chart, and checks the answers against hand calculations.
 *
 * The fake engine is the same as the one used for the SJF / SRTF tests,
 * so the results can be compared.
 *
 * Simplifications: no I/O, context-switch cost = 0.
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "scheduler.h"

extern Scheduler rr_scheduler;
extern int rr_set_quantum(int q);
extern int rr_get_quantum(void);

#define MAX_TIME 200

/* Filled by run(): the Gantt chart as text, and the number of preemptions. */
static char last_gantt[MAX_TIME + 1];
static int  last_preemptions;

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
         *    If yes, it goes back to the ready queue. */
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
    printf("Policy: %s (quantum = %d)\n", s->name, rr_get_quantum());
    printf("Gantt : %s   (. = idle)\n", last_gantt);

    double sum_wait = 0, sum_tat = 0, sum_resp = 0;
    printf("PID  arrival burst  start finish  wait  turnaround response\n");
    for (int i = 0; i < n; i++) {
        int turnaround = procs[i].finish - procs[i].arrival;
        int wait = turnaround - procs[i].burst;
        int response = procs[i].first_run - procs[i].arrival;
        sum_wait += wait;
        sum_tat += turnaround;
        sum_resp += response;
        printf("P%-3d %5d %5d %6d %6d %5d %10d %8d\n",
               procs[i].pid, procs[i].arrival, procs[i].burst,
               procs[i].first_run, procs[i].finish,
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

/* ===================== ROUND ROBIN TESTS ===================== */

/* TEST 1: normal case, calculated by hand. Quantum = 2.
 * P1(arr 0, burst 5) P2(arr 1, burst 3) P3(arr 2, burst 1) P4(arr 3, burst 2)
 *
 *   time 0-2 : P1 runs (3 left).  Queue at time 2: P2, P3, then P1 goes back
 *   time 2-4 : P2 runs (1 left).  P4 arrived at 3. Queue: P3, P1, P4, P2
 *   time 4-5 : P3 runs and finishes (burst shorter than the quantum)
 *   time 5-7 : P1 runs (1 left).  Queue: P4, P2, P1
 *   time 7-9 : P4 runs and finishes
 *   time 9-10: P2 runs and finishes
 *   time10-11: P1 runs and finishes
 *
 * Expected Gantt: 11 22 3 11 44 2 1
 * Finish:  P1=11 P2=10 P3=5 P4=9
 * Waiting: P1=6  P2=6  P3=2 P4=4  -> average 4.5 */
static void test_rr_normal(void)
{
    printf("=== RR TEST 1: normal case, quantum 2 ===\n");
    Process p[4];
    make_process(&p[0], 1, 0, 5);
    make_process(&p[1], 2, 1, 3);
    make_process(&p[2], 3, 2, 1);
    make_process(&p[3], 4, 3, 2);
    rr_set_quantum(2);
    run(&rr_scheduler, p, 4);

    assert(strcmp(last_gantt, "11223114421") == 0);
    assert(p[0].finish == 11);
    assert(p[1].finish == 10);
    assert(p[2].finish == 5);
    assert(p[3].finish == 9);
    assert(wait_of(&p[0]) == 6);
    assert(wait_of(&p[1]) == 6);
    assert(wait_of(&p[2]) == 2);
    assert(wait_of(&p[3]) == 4);
    assert(response_of(&p[3]) == 4);   /* P4 arrives at 3, first runs at 7 */
    assert(last_preemptions == 3);     /* P1 at 2, P2 at 4, P1 at 7 */
}

/* TEST 2: single process that arrives late (idle gap). */
static void test_rr_idle_gap(void)
{
    printf("=== RR TEST 2: single process, CPU idle at the start ===\n");
    Process p[1];
    make_process(&p[0], 1, 3, 2);
    rr_set_quantum(4);
    run(&rr_scheduler, p, 1);

    assert(strcmp(last_gantt, "...11") == 0);
    assert(p[0].first_run == 3);
    assert(p[0].finish == 5);
    assert(wait_of(&p[0]) == 0);
}

/* TEST 3: single process LONGER than the quantum.
 * The quantum ends 3 times but nobody is waiting, so the process is never
 * stopped (rule 3) and there are 0 preemptions. */
static void test_rr_single_long(void)
{
    printf("=== RR TEST 3: single process longer than the quantum ===\n");
    Process p[1];
    make_process(&p[0], 1, 0, 7);
    rr_set_quantum(2);
    run(&rr_scheduler, p, 1);

    assert(p[0].finish == 7);
    assert(wait_of(&p[0]) == 0);
    assert(last_preemptions == 0);
}

/* TEST 4: all arrive at time 0 with EQUAL bursts (tie rule).
 * RR does not compare bursts: the order is the order of arrival in the
 * queue (P1, P2, P3), and it repeats every round.
 * Quantum = 2, bursts = 4.
 * Expected Gantt: 11 22 33 11 22 33
 * Finish: P1=8 P2=10 P3=12 */
static void test_rr_all_at_zero_equal(void)
{
    printf("=== RR TEST 4: all arrive at time 0, equal bursts ===\n");
    Process p[3];
    make_process(&p[0], 1, 0, 4);
    make_process(&p[1], 2, 0, 4);
    make_process(&p[2], 3, 0, 4);
    rr_set_quantum(2);
    run(&rr_scheduler, p, 3);

    assert(strcmp(last_gantt, "112233112233") == 0);
    assert(p[0].finish == 8);
    assert(p[1].finish == 10);
    assert(p[2].finish == 12);
    /* Fair response: nobody waits more than 2 quanta for the first turn. */
    assert(response_of(&p[0]) == 0);
    assert(response_of(&p[1]) == 2);
    assert(response_of(&p[2]) == 4);
}

/* TEST 5: quantum larger than all bursts -> behaves exactly like FCFS.
 * P1(arr 0, burst 6) P2(arr 0, burst 2) P3(arr 0, burst 4), quantum 10.
 * Expected Gantt: 111111 22 3333, no preemption.
 * Finish: P1=6 P2=8 P3=12 */
static void test_rr_big_quantum(void)
{
    printf("=== RR TEST 5: quantum larger than all bursts (= FCFS) ===\n");
    Process p[3];
    make_process(&p[0], 1, 0, 6);
    make_process(&p[1], 2, 0, 2);
    make_process(&p[2], 3, 0, 4);
    rr_set_quantum(10);
    run(&rr_scheduler, p, 3);

    assert(strcmp(last_gantt, "111111223333") == 0);
    assert(p[0].finish == 6);
    assert(p[1].finish == 8);
    assert(p[2].finish == 12);
    assert(last_preemptions == 0);
}

/* TEST 6: smallest quantum (1 tick) -> the processes take turns every tick.
 * P1(arr 0, burst 3) P2(arr 0, burst 2).
 * Expected Gantt: 1 2 1 2 1
 * Finish: P1=5 P2=4 */
static void test_rr_quantum_one(void)
{
    printf("=== RR TEST 6: quantum 1 ===\n");
    Process p[2];
    make_process(&p[0], 1, 0, 3);
    make_process(&p[1], 2, 0, 2);
    rr_set_quantum(1);
    run(&rr_scheduler, p, 2);

    assert(strcmp(last_gantt, "12121") == 0);
    assert(p[0].finish == 5);
    assert(p[1].finish == 4);
    assert(last_preemptions == 3);
}

/* TEST 7: a process arrives at the SAME tick the quantum ends (rule 2).
 * P1(arr 0, burst 4) P2(arr 2, burst 2), quantum 2.
 * At time 2 the quantum of P1 ends and P2 arrives. P2 enters the queue
 * first, so P2 runs before P1 continues.
 * Expected Gantt: 11 22 11
 * Finish: P1=6 P2=4   (with the opposite rule it would be P1=4 P2=6) */
static void test_rr_arrival_at_quantum_end(void)
{
    printf("=== RR TEST 7: arrival at the same tick the quantum ends ===\n");
    Process p[2];
    make_process(&p[0], 1, 0, 4);
    make_process(&p[1], 2, 2, 2);
    rr_set_quantum(2);
    run(&rr_scheduler, p, 2);

    assert(strcmp(last_gantt, "112211") == 0);
    assert(p[1].finish == 4);
    assert(p[0].finish == 6);
    assert(response_of(&p[1]) == 0);
}

/* TEST 8: fresh quantum when the queue was empty (rule 3).
 * P1(arr 0, burst 6) P2(arr 3, burst 2), quantum 2.
 * At time 2 the quantum of P1 ends but nobody waits -> P1 gets a new
 * quantum (time 2-4). P2 arrives at 3 and must wait until time 4.
 * Expected Gantt: 1111 22 11
 * Finish: P1=8 P2=6 */
static void test_rr_fresh_quantum(void)
{
    printf("=== RR TEST 8: fresh quantum when nobody was waiting ===\n");
    Process p[2];
    make_process(&p[0], 1, 0, 6);
    make_process(&p[1], 2, 3, 2);
    rr_set_quantum(2);
    run(&rr_scheduler, p, 2);

    assert(strcmp(last_gantt, "11112211") == 0);
    assert(p[1].first_run == 4);
    assert(p[1].finish == 6);
    assert(p[0].finish == 8);
    assert(last_preemptions == 1);
}

/* TEST 9: idle gap in the MIDDLE, then the queue is used again.
 * P1(arr 0, burst 2) P2(arr 5, burst 3) P3(arr 5, burst 3), quantum 2.
 * Expected Gantt: 11 ... 22 33 2 3
 * Finish: P1=2 P2=10 P3=11 */
static void test_rr_idle_in_middle(void)
{
    printf("=== RR TEST 9: CPU idle in the middle ===\n");
    Process p[3];
    make_process(&p[0], 1, 0, 2);
    make_process(&p[1], 2, 5, 3);
    make_process(&p[2], 3, 5, 3);
    rr_set_quantum(2);
    run(&rr_scheduler, p, 3);

    assert(strcmp(last_gantt, "11...223323") == 0);
    assert(p[0].finish == 2);
    assert(p[1].finish == 10);
    assert(p[2].finish == 11);
}

/* TEST 10: convoy effect - why RR helps short jobs.
 * P1(arr 0, burst 10) P2(arr 1, burst 1) P3(arr 2, burst 1).
 * With a huge quantum (= FCFS) the short jobs wait behind P1:
 *   response P2 = 9, P3 = 9.
 * With quantum 2 they run early:
 *   Gantt: 11 2 3 11111111, response P2 = 1, P3 = 1. */
static void test_rr_convoy(void)
{
    printf("=== RR TEST 10: short jobs behind a long job ===\n");
    Process p[3];

    make_process(&p[0], 1, 0, 10);
    make_process(&p[1], 2, 1, 1);
    make_process(&p[2], 3, 2, 1);
    rr_set_quantum(100);
    run(&rr_scheduler, p, 3);
    assert(response_of(&p[1]) == 9);
    assert(response_of(&p[2]) == 9);

    make_process(&p[0], 1, 0, 10);
    make_process(&p[1], 2, 1, 1);
    make_process(&p[2], 3, 2, 1);
    rr_set_quantum(2);
    run(&rr_scheduler, p, 3);
    assert(strcmp(last_gantt, "112311111111") == 0);
    assert(response_of(&p[1]) == 1);
    assert(response_of(&p[2]) == 1);
    assert(p[0].finish == 12);
}

/* TEST 11: bad quantum values are rejected and the old value is kept. */
static void test_rr_bad_quantum(void)
{
    printf("=== RR TEST 11: invalid quantum is rejected ===\n");
    assert(rr_set_quantum(3) == 0);
    assert(rr_set_quantum(0) == -1);
    assert(rr_set_quantum(-5) == -1);
    assert(rr_get_quantum() == 3);     /* still the last good value */
    printf("quantum 0 and -5 rejected, quantum is still %d\n\n",
           rr_get_quantum());
}

int main(void)
{
    test_rr_normal();
    test_rr_idle_gap();
    test_rr_single_long();
    test_rr_all_at_zero_equal();
    test_rr_big_quantum();
    test_rr_quantum_one();
    test_rr_arrival_at_quantum_end();
    test_rr_fresh_quantum();
    test_rr_idle_in_middle();
    test_rr_convoy();
    test_rr_bad_quantum();
    printf("ALL RR TESTS PASSED\n");
    return 0;
}
