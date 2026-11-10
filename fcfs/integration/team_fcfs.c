#include "team_fcfs.h"
#include "fcfs.h"

#include <stdint.h>
#include <string.h>

static FcfsQueue ready_queue;
static Process *bound_processes;
static size_t process_count;
static bool queued[FCFS_CAPACITY];
static const char *last_error;

const char *team_fcfs_error(void)
{
    return last_error;
}

void team_fcfs_cleanup(void)
{
    FCFS_POLICY.cleanup(&ready_queue);
    bound_processes = NULL;
    process_count = 0U;
    memset(queued, 0, sizeof queued);
    last_error = NULL;
}

static void policy_init(void)
{
    last_error = NULL;
    memset(queued, 0, sizeof queued);
    if (bound_processes == NULL || !FCFS_POLICY.init(&ready_queue, process_count)) {
        last_error = "FCFS requires a bound array of 1..1024 processes before init";
    }
}

bool team_fcfs_bind(Process *processes, size_t count)
{
    team_fcfs_cleanup();
    if (processes == NULL || count == 0U || count > FCFS_CAPACITY) {
        last_error = "FCFS bind requires a stable array of 1..1024 processes";
        return false;
    }
    bound_processes = processes;
    process_count = count;
    policy_init();
    return last_error == NULL;
}

/* On ordinary flat-address systems uintptr_t permits O(1) address validation
   without subtracting unrelated Process pointers or dereferencing the input. */
static bool process_index(Process *process, size_t *index)
{
    if (process == NULL || bound_processes == NULL) {
        last_error = "FCFS callback requires a process in the bound array";
        return false;
    }
    uintptr_t base = (uintptr_t)(void *)bound_processes;
    uintptr_t address = (uintptr_t)(void *)process;
    if (address < base) {
        last_error = "FCFS process is outside the bound array";
        return false;
    }
    uintptr_t offset = address - base;
    if (offset % sizeof(Process) != 0U || offset / sizeof(Process) >= process_count) {
        last_error = "FCFS process is outside or misaligned in the bound array";
        return false;
    }
    *index = (size_t)(offset / sizeof(Process));
    return true;
}

static void enqueue_process(Process *process)
{
    size_t index;
    if (last_error != NULL || !process_index(process, &index)) {
        return;
    }
    if (queued[index]) {
        last_error = "FCFS process is already in the ready queue";
        return;
    }
    if (!FCFS_POLICY.on_arrival(&ready_queue, index)) {
        last_error = "FCFS ready queue is full or uninitialized";
        return;
    }
    queued[index] = true;
}

static Process *pick_next(void)
{
    size_t index;
    if (last_error != NULL) {
        return NULL;
    }
    if (bound_processes == NULL) {
        last_error = "FCFS pick_next requires a bound process array";
        return NULL;
    }
    if (!FCFS_POLICY.pick_next(&ready_queue, &index)) {
        return NULL;
    }
    if (index >= process_count || !queued[index]) {
        last_error = "FCFS ready queue contains an invalid index";
        return NULL;
    }
    queued[index] = false;
    return &bound_processes[index];
}

static int should_preempt(Process *running)
{
    (void)running;
    return 0;
}

static void on_exit(Process *process)
{
    /* The engine owns CPU completion and I/O transitions. */
    (void)process;
}

Scheduler fcfs_scheduler = {
    "FCFS", policy_init, enqueue_process, enqueue_process,
    pick_next, should_preempt, on_exit
};
