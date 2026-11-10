#ifndef SIMULATION_H
#define SIMULATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SIM_MAX_PROCESSES 1024U
#define SIM_MAX_BURSTS 31U
/* This bound leaves room for additions and burst totals without overflow. */
#define SIM_MAX_TIME (INT64_MAX / INT64_C(64))
#define SIM_ERROR_SIZE 256U

typedef enum { NEW, READY, RUNNING, BLOCKED, DONE } ProcessState;

typedef struct {
    int pid;
    int64_t arrival;
    int priority;
    size_t burst_count;
    int64_t bursts[SIM_MAX_BURSTS]; /* Even positions CPU, odd positions I/O. */
} ProcessSpec;

typedef struct {
    size_t count;
    ProcessSpec processes[SIM_MAX_PROCESSES]; /* Input order never changes. */
} Workload;

typedef struct {
    int64_t switch_cost;
    int64_t tick_limit;
    int64_t starvation_threshold;
} SimConfig;

typedef struct {
    ProcessState state;
    size_t burst_index;
    int64_t remaining;
    int64_t io_ready_at;
    int64_t first_run; /* -1 until useful execution starts. */
    int64_t completion;
    int64_t cpu_time;
    int64_t io_time;
    int64_t waiting;
    int64_t ready_since;
    int64_t max_ready_wait;
} ProcessResult;

typedef struct {
    size_t count;
    size_t completed;
    ProcessResult processes[SIM_MAX_PROCESSES];
    int64_t total_ticks;
    int64_t run_ticks;
    int64_t idle_ticks;
    int64_t switch_ticks;
    int64_t context_switches;
    int64_t max_ready_wait;
    size_t excessive_wait_processes;
    double average_waiting;
    double average_turnaround;
    double average_response;
    double throughput_per_100_ticks;
    double useful_cpu_utilization; /* Fraction, not percent. */
    double jain_fairness;
} SimResult;

SimConfig sim_default_config(void);
bool sim_validate_config(const SimConfig *config, char *error, size_t error_size);
bool workload_validate(const Workload *workload, char *error, size_t error_size);
bool workload_load(FILE *input, Workload *workload, char *error, size_t error_size);
bool workload_load_path(const char *path, Workload *workload,
                        char *error, size_t error_size);
/* All streams are caller-owned; trace may be NULL. Result is valid on success. */
bool simulation_run(const Workload *workload, const SimConfig *config,
                    FILE *trace, SimResult *result, char *error, size_t error_size);
bool simulation_verify(const Workload *workload, const SimResult *result,
                       char *error, size_t error_size);
void simulation_print(FILE *output, const Workload *workload,
                      const SimConfig *config, const SimResult *result);
bool simulation_write_json(FILE *output, const Workload *workload,
                           const SimConfig *config, const SimResult *result);

#endif
