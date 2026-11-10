#include "simulation.h"
#include "fcfs.h"

#include <inttypes.h>
#include <stdarg.h>
#include <string.h>

#define NO_PROCESS SIZE_MAX

static bool fail(char *error, size_t size, const char *format, ...)
{
    if (error != NULL && size > 0U) {
        va_list args;
        va_start(args, format);
        (void)vsnprintf(error, size, format, args);
        va_end(args);
    }
    return false;
}

SimConfig sim_default_config(void)
{
    SimConfig config = {1, 1000000, 50};
    return config;
}

bool sim_validate_config(const SimConfig *config, char *error, size_t size)
{
    if (config == NULL || config->switch_cost < 0 ||
        config->switch_cost > SIM_MAX_TIME || config->tick_limit <= 0 ||
        config->tick_limit > SIM_MAX_TIME || config->starvation_threshold < 0 ||
        config->starvation_threshold > SIM_MAX_TIME) {
        return fail(error, size, "invalid config: tick limit must be positive; switch cost and threshold nonnegative; all <= %" PRId64,
                    SIM_MAX_TIME);
    }
    return true;
}

static bool trace_tick(FILE *trace, int64_t time, const char *kind, int pid)
{
    if (trace == NULL) {
        return true;
    }
    if (pid == 0) {
        return fprintf(trace, "%" PRId64 ",%" PRId64 ",%s,\n", time, time + 1, kind) >= 0;
    }
    return fprintf(trace, "%" PRId64 ",%" PRId64 ",%s,%d\n", time, time + 1, kind, pid) >= 0;
}

/* This checks the engine's ownership rules, independently of the policy. */
static bool check_states(const SimResult *result, const FcfsQueue *queue,
                         size_t active, char *error, size_t size)
{
    size_t running = 0U;
    size_t ready = 0U;
    for (size_t i = 0U; i < result->count; ++i) {
        if (result->processes[i].state == RUNNING) {
            ++running;
            if (i != active) {
                return fail(error, size, "invariant failed: running process is not active");
            }
        } else if (result->processes[i].state == READY) {
            ++ready;
        }
    }
    size_t reserved = active != NO_PROCESS && result->processes[active].state == READY ? 1U : 0U;
    if (running > 1U || ready != queue->count + reserved) {
        return fail(error, size, "invariant failed: CPU or ready-queue ownership");
    }
    return true;
}

static void calculate_metrics(const Workload *workload, const SimConfig *config,
                              SimResult *result)
{
    double waiting = 0.0;
    double turnaround = 0.0;
    double response = 0.0;
    double sum_x = 0.0;
    double sum_x_squared = 0.0;
    for (size_t i = 0U; i < result->count; ++i) {
        ProcessResult *p = &result->processes[i];
        int64_t turn = p->completion - workload->processes[i].arrival;
        waiting += (double)p->waiting;
        turnaround += (double)turn;
        response += (double)(p->first_run - workload->processes[i].arrival);
        double x = (double)p->cpu_time / (double)turn;
        sum_x += x;
        sum_x_squared += x * x;
        if (p->max_ready_wait > result->max_ready_wait) {
            result->max_ready_wait = p->max_ready_wait;
        }
        if (p->max_ready_wait > config->starvation_threshold) {
            ++result->excessive_wait_processes;
        }
    }
    double n = (double)result->count;
    result->average_waiting = waiting / n;
    result->average_turnaround = turnaround / n;
    result->average_response = response / n;
    result->throughput_per_100_ticks = 100.0 * n / (double)result->total_ticks;
    result->useful_cpu_utilization = (double)result->run_ticks / (double)result->total_ticks;
    result->jain_fairness = (sum_x * sum_x) / (n * sum_x_squared);
}

bool simulation_run(const Workload *workload, const SimConfig *config,
                    FILE *trace, SimResult *result, char *error, size_t size)
{
    if (result == NULL) {
        return fail(error, size, "missing simulation result destination");
    }
    if (!workload_validate(workload, error, size) ||
        !sim_validate_config(config, error, size)) {
        return false;
    }
    memset(result, 0, sizeof(*result));
    result->count = workload->count;
    FcfsQueue queue;
    if (!FCFS_POLICY.init(&queue, workload->count)) {
        return fail(error, size, "cannot initialize FCFS queue");
    }
    for (size_t i = 0U; i < result->count; ++i) {
        result->processes[i].state = NEW;
        result->processes[i].remaining = workload->processes[i].bursts[0];
        result->processes[i].first_run = -1;
    }
    bool ok = false;
    size_t active = NO_PROCESS;
    int last_pid = 0;
    int64_t switch_remaining = 0;
    int64_t time = 0;
    if (trace != NULL && fprintf(trace, "start,end,kind,pid\n") < 0) {
        (void)fail(error, size, "cannot write trace header");
        goto cleanup;
    }
    for (;;) {
        /* Boundary 1: commit the CPU work performed in [time-1,time). */
        if (active != NO_PROCESS) {
            ProcessResult *p = &result->processes[active];
            const ProcessSpec *spec = &workload->processes[active];
            if (p->state == RUNNING && p->remaining == 0) {
                if (p->burst_index + 1U == spec->burst_count) {
                    p->state = DONE;
                    p->completion = time;
                    ++result->completed;
                } else {
                    int64_t io = spec->bursts[p->burst_index + 1U];
                    p->state = BLOCKED;
                    p->io_ready_at = time + io;
                    p->io_time += io;
                    p->burst_index += 2U;
                    p->remaining = spec->bursts[p->burst_index];
                }
                active = NO_PROCESS;
            }
        }
        /* Boundary 2: new arrivals, preserving the original CSV row order. */
        for (size_t i = 0U; i < result->count; ++i) {
            ProcessResult *p = &result->processes[i];
            if (p->state == NEW && workload->processes[i].arrival == time) {
                p->state = READY;
                p->ready_since = time;
                if (!FCFS_POLICY.on_arrival(&queue, i)) {
                    (void)fail(error, size, "FCFS arrival queue overflow");
                    goto cleanup;
                }
            }
        }
        /* Boundary 3: independent I/O completions, also in input order.
           A zero-length I/O begun above is eligible in this same pass. */
        for (size_t i = 0U; i < result->count; ++i) {
            ProcessResult *p = &result->processes[i];
            if (p->state == BLOCKED && p->io_ready_at == time) {
                p->state = READY;
                p->ready_since = time;
                if (!FCFS_POLICY.on_unblock(&queue, i)) {
                    (void)fail(error, size, "FCFS unblock queue overflow");
                    goto cleanup;
                }
            }
        }
        if (result->completed == result->count) {
            break;
        }
        if (time >= config->tick_limit) {
            (void)fail(error, size, "simulation tick limit reached at %" PRId64 " (%zu/%zu completed)",
                       time, result->completed, result->count);
            goto cleanup;
        }
        /* FCFS never preempts a running burst. Selection only occurs when idle.
           During switching, active is reserved and remains READY. */
        if (active == NO_PROCESS && queue.count > 0U) {
            if (!FCFS_POLICY.pick_next(&queue, &active) || active >= result->count) {
                (void)fail(error, size, "invalid FCFS selection");
                goto cleanup;
            }
            int pid = workload->processes[active].pid;
            if (last_pid != 0 && pid != last_pid) {
                ++result->context_switches;
                switch_remaining = config->switch_cost;
            }
            last_pid = pid; /* Retained across both blocking and idle periods. */
        }
        const char *kind;
        int trace_pid = 0;
        if (active == NO_PROCESS) {
            kind = "IDLE";
            ++result->idle_ticks;
        } else if (switch_remaining > 0) {
            kind = "SWITCH";
            trace_pid = workload->processes[active].pid;
            --switch_remaining;
            ++result->switch_ticks;
        } else {
            ProcessResult *p = &result->processes[active];
            if (p->state == READY) {
                int64_t wait = time - p->ready_since;
                if (wait > p->max_ready_wait) {
                    p->max_ready_wait = wait;
                }
                p->state = RUNNING;
                if (p->first_run < 0) {
                    p->first_run = time;
                }
            }
            kind = "RUN";
            trace_pid = workload->processes[active].pid;
            --p->remaining;
            ++p->cpu_time;
            ++result->run_ticks;
        }
        if (!check_states(result, &queue, active, error, size)) {
            goto cleanup;
        }
        for (size_t i = 0U; i < result->count; ++i) {
            if (result->processes[i].state == READY) {
                ++result->processes[i].waiting;
            }
        }
        if (!trace_tick(trace, time, kind, trace_pid)) {
            (void)fail(error, size, "cannot write per-tick trace");
            goto cleanup;
        }
        ++time;
        result->total_ticks = time;
    }
    if (trace != NULL && fflush(trace) != 0) {
        (void)fail(error, size, "cannot flush trace");
        goto cleanup;
    }
    calculate_metrics(workload, config, result);
    ok = simulation_verify(workload, result, error, size);
cleanup:
    FCFS_POLICY.cleanup(&queue);
    return ok;
}

bool simulation_verify(const Workload *workload, const SimResult *result,
                       char *error, size_t size)
{
    if (!workload_validate(workload, error, size)) {
        return false;
    }
    if (result == NULL || result->count != workload->count ||
        result->completed != workload->count || result->total_ticks <= 0 ||
        result->total_ticks > SIM_MAX_TIME || result->run_ticks < 0 ||
        result->idle_ticks < 0 || result->switch_ticks < 0 ||
        result->run_ticks > result->total_ticks || result->idle_ticks > result->total_ticks ||
        result->switch_ticks > result->total_ticks ||
        result->run_ticks + result->idle_ticks + result->switch_ticks != result->total_ticks) {
        return fail(error, size, "invariant failed: completion or total time conservation");
    }
    int64_t cpu_sum = 0;
    for (size_t i = 0U; i < result->count; ++i) {
        const ProcessSpec *spec = &workload->processes[i];
        const ProcessResult *p = &result->processes[i];
        int64_t expected_cpu = 0;
        int64_t expected_io = 0;
        for (size_t j = 0U; j < spec->burst_count; ++j) {
            if (j % 2U == 0U) {
                expected_cpu += spec->bursts[j];
            } else {
                expected_io += spec->bursts[j];
            }
        }
        if (p->state != DONE || p->cpu_time != expected_cpu || p->io_time != expected_io ||
            p->waiting < 0 || p->waiting > result->total_ticks || p->max_ready_wait < 0 ||
            p->max_ready_wait > p->waiting || p->first_run < spec->arrival ||
            p->first_run >= p->completion || p->completion > result->total_ticks ||
            p->completion - spec->arrival != p->cpu_time + p->io_time + p->waiting) {
            return fail(error, size, "invariant failed: PID %d service or turnaround conservation", spec->pid);
        }
        if (cpu_sum > result->run_ticks - p->cpu_time) {
            return fail(error, size, "invariant failed: process CPU total exceeds RUN ticks");
        }
        cpu_sum += p->cpu_time;
    }
    if (cpu_sum != result->run_ticks) {
        return fail(error, size, "invariant failed: CPU service differs from RUN ticks");
    }
    return true;
}

void simulation_print(FILE *output, const Workload *workload,
                      const SimConfig *config, const SimResult *result)
{
    (void)fprintf(output, "FCFS reference simulator (1 tick = 1 ms)\n");
    (void)fprintf(output, "Switch cost: %" PRId64 " | Tick limit: %" PRId64 " | Excessive-wait threshold: %" PRId64 "\n",
                  config->switch_cost, config->tick_limit, config->starvation_threshold);
    (void)fprintf(output, "PID  Arrival Priority CPU   I/O   First  Finish Wait  Turn   Resp  MaxReady\n");
    for (size_t i = 0U; i < result->count; ++i) {
        const ProcessSpec *s = &workload->processes[i];
        const ProcessResult *p = &result->processes[i];
        (void)fprintf(output, "%-4d %-7" PRId64 " %-8d %-5" PRId64 " %-5" PRId64 " %-6" PRId64 " %-6" PRId64 " %-5" PRId64 " %-6" PRId64 " %-5" PRId64 " %" PRId64 "\n",
                      s->pid, s->arrival, s->priority, p->cpu_time, p->io_time,
                      p->first_run, p->completion, p->waiting, p->completion - s->arrival,
                      p->first_run - s->arrival, p->max_ready_wait);
    }
    (void)fprintf(output, "Completed: %zu/%zu | Total: %" PRId64 " | RUN: %" PRId64 " | IDLE: %" PRId64 " | SWITCH: %" PRId64 "\n",
                  result->completed, result->count, result->total_ticks, result->run_ticks,
                  result->idle_ticks, result->switch_ticks);
    (void)fprintf(output, "Average waiting: %.6f | Turnaround: %.6f | Response: %.6f\n",
                  result->average_waiting, result->average_turnaround, result->average_response);
    (void)fprintf(output, "Throughput/100 ticks: %.6f | Useful CPU utilization: %.6f%%\n",
                  result->throughput_per_100_ticks, 100.0 * result->useful_cpu_utilization);
    (void)fprintf(output, "Context switches: %" PRId64 " | Max continuous ready wait: %" PRId64 " | Excessive-wait processes: %zu\n",
                  result->context_switches, result->max_ready_wait, result->excessive_wait_processes);
    (void)fprintf(output, "Jain fairness (CPU service / turnaround): %.9f\nInvariants: PASS\n",
                  result->jain_fairness);
    (void)fprintf(output, "Excessive waiting is an indicator, not proof of indefinite starvation.\n");
}

bool simulation_write_json(FILE *output, const Workload *workload,
                           const SimConfig *config, const SimResult *result)
{
    if (output == NULL) {
        return false;
    }
    (void)fprintf(output, "{\n  \"policy\": \"FCFS\",\n  \"tick_ms\": 1,\n  \"config\": {\"switch_cost\": %" PRId64 ", \"tick_limit\": %" PRId64 ", \"starvation_threshold\": %" PRId64 "},\n",
                  config->switch_cost, config->tick_limit, config->starvation_threshold);
    (void)fprintf(output, "  \"processes\": [\n");
    for (size_t i = 0U; i < result->count; ++i) {
        const ProcessSpec *s = &workload->processes[i];
        const ProcessResult *p = &result->processes[i];
        (void)fprintf(output, "    {\"pid\": %d, \"arrival\": %" PRId64 ", \"priority\": %d, \"state\": \"DONE\", \"cpu_time\": %" PRId64 ", \"io_time\": %" PRId64 ", \"first_run\": %" PRId64 ", \"completion\": %" PRId64 ", \"waiting\": %" PRId64 ", \"turnaround\": %" PRId64 ", \"response\": %" PRId64 ", \"max_continuous_ready_wait\": %" PRId64 ", \"excessive_wait\": %s}%s\n",
                      s->pid, s->arrival, s->priority, p->cpu_time, p->io_time, p->first_run,
                      p->completion, p->waiting, p->completion - s->arrival, p->first_run - s->arrival,
                      p->max_ready_wait, p->max_ready_wait > config->starvation_threshold ? "true" : "false",
                      i + 1U == result->count ? "" : ",");
    }
    (void)fprintf(output, "  ],\n  \"summary\": {\n    \"completed\": %zu,\n    \"total_ticks\": %" PRId64 ",\n    \"run_ticks\": %" PRId64 ",\n    \"idle_ticks\": %" PRId64 ",\n    \"switch_ticks\": %" PRId64 ",\n    \"context_switches\": %" PRId64 ",\n",
                  result->completed, result->total_ticks, result->run_ticks,
                  result->idle_ticks, result->switch_ticks, result->context_switches);
    (void)fprintf(output, "    \"average_waiting\": %.12g,\n    \"average_turnaround\": %.12g,\n    \"average_response\": %.12g,\n    \"throughput_per_100_ticks\": %.12g,\n    \"useful_cpu_utilization\": %.12g,\n    \"max_continuous_ready_wait\": %" PRId64 ",\n    \"excessive_wait_processes\": %zu,\n    \"jain_fairness\": %.12g,\n    \"invariants_passed\": true\n  }\n}\n",
                  result->average_waiting, result->average_turnaround, result->average_response,
                  result->throughput_per_100_ticks, result->useful_cpu_utilization,
                  result->max_ready_wait, result->excessive_wait_processes, result->jain_fairness);
    return fflush(output) == 0 && ferror(output) == 0;
}
