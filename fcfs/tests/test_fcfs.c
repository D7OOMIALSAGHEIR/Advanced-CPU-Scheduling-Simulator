#include "fcfs.h"
#include "simulation.h"

#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Large process arrays live on the heap, including on Windows. */
static Workload *workload;
static SimResult *result;
static SimResult *second_result;
static char error_text[SIM_ERROR_SIZE];

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        if (error_text[0] != '\0') { fprintf(stderr, "  %s\n", error_text); } \
        return false; \
    } \
} while (0)

typedef struct {
    const char *kind;
    int pid; /* Zero represents the blank IDLE field. */
    int64_t ticks;
} ExpectedInterval;

static bool close_enough(double actual, double expected)
{
    double difference = actual - expected;
    if (difference < 0.0) { difference = -difference; }
    return difference < 0.000000001;
}

static bool load_text(const char *text)
{
    bool ok;
    FILE *input = tmpfile();
    if (input == NULL) { return false; }
    if (fputs(text, input) == EOF) { fclose(input); return false; }
    rewind(input);
    error_text[0] = '\0';
    ok = workload_load(input, workload, error_text, sizeof error_text);
    if (fclose(input) != 0) { return false; }
    return ok;
}

static bool run_config(SimConfig config, FILE *trace)
{
    error_text[0] = '\0';
    return simulation_run(workload, &config, trace, result,
                          error_text, sizeof error_text) &&
           simulation_verify(workload, result, error_text, sizeof error_text);
}

static bool expect_trace(FILE *trace, const ExpectedInterval *expected,
                         size_t interval_count)
{
    char actual[160];
    char wanted[160];
    int64_t tick = 0;
    size_t interval;
    rewind(trace);
    CHECK(fgets(actual, sizeof actual, trace) != NULL);
    CHECK(strcmp(actual, "start,end,kind,pid\n") == 0);
    for (interval = 0U; interval < interval_count; ++interval) {
        int64_t j;
        for (j = 0; j < expected[interval].ticks; ++j) {
            int count;
            if (expected[interval].pid == 0) {
                count = snprintf(wanted, sizeof wanted, "%" PRId64 ",%" PRId64 ",%s,\n",
                                 tick, tick + 1, expected[interval].kind);
            } else {
                count = snprintf(wanted, sizeof wanted, "%" PRId64 ",%" PRId64 ",%s,%d\n",
                                 tick, tick + 1, expected[interval].kind,
                                 expected[interval].pid);
            }
            CHECK(count > 0 && (size_t)count < sizeof wanted);
            CHECK(fgets(actual, sizeof actual, trace) != NULL);
            if (strcmp(actual, wanted) != 0) {
                fprintf(stderr, "Expected: %sActual:   %s", wanted, actual);
                CHECK(false);
            }
            ++tick;
        }
    }
    CHECK(fgets(actual, sizeof actual, trace) == NULL);
    CHECK(!ferror(trace));
    CHECK(tick == result->total_ticks);
    return true;
}

static bool same_streams(FILE *first, FILE *second)
{
    int a;
    int b;
    rewind(first);
    rewind(second);
    do {
        a = fgetc(first);
        b = fgetc(second);
        if (a != b) { return false; }
    } while (a != EOF);
    return !ferror(first) && !ferror(second);
}

static bool test_queue(void)
{
    FcfsQueue queue;
    size_t value = 99U;
    size_t round;
    CHECK(!fcfs_init(NULL, 3U));
    CHECK(!fcfs_init(&queue, 0U));
    CHECK(!fcfs_init(&queue, FCFS_CAPACITY + 1U));
    CHECK(FCFS_POLICY.init(&queue, 3U));
    CHECK(!FCFS_POLICY.pick_next(&queue, &value));
    CHECK(value == 99U);
    CHECK(FCFS_POLICY.on_arrival(&queue, 7U));
    CHECK(FCFS_POLICY.on_unblock(&queue, 2U));
    CHECK(fcfs_enqueue(&queue, 9U));
    CHECK(!fcfs_enqueue(&queue, 1U));
    CHECK(!fcfs_dequeue(&queue, NULL));
    CHECK(fcfs_dequeue(&queue, &value) && value == 7U);
    CHECK(fcfs_enqueue(&queue, 4U)); /* Tail wraps to position zero. */
    CHECK(fcfs_dequeue(&queue, &value) && value == 2U);
    CHECK(fcfs_dequeue(&queue, &value) && value == 9U);
    CHECK(fcfs_dequeue(&queue, &value) && value == 4U);
    CHECK(!fcfs_dequeue(&queue, &value));
    for (round = 0U; round < 100U; ++round) {
        CHECK(fcfs_enqueue(&queue, round));
        CHECK(!FCFS_POLICY.should_preempt(&queue, round));
        CHECK(fcfs_dequeue(&queue, &value) && value == round);
    }
    CHECK(!fcfs_enqueue(&queue, FCFS_CAPACITY));
    CHECK(fcfs_init(&queue, FCFS_CAPACITY));
    for (round = 0U; round < FCFS_CAPACITY; ++round) {
        CHECK(fcfs_enqueue(&queue, round));
    }
    CHECK(!fcfs_enqueue(&queue, 0U));
    for (round = 0U; round < FCFS_CAPACITY; ++round) {
        CHECK(fcfs_dequeue(&queue, &value) && value == round);
    }
    FCFS_POLICY.cleanup(&queue);
    CHECK(queue.capacity == 0U && queue.count == 0U);
    CHECK(!fcfs_enqueue(&queue, 0U));
    CHECK(!fcfs_dequeue(&queue, &value));
    fcfs_cleanup(NULL);
    return true;
}

static bool test_basic_metrics(void)
{
    const ExpectedInterval expected[] = {{"RUN", 1, 5}, {"RUN", 2, 3}, {"RUN", 3, 1}};
    SimConfig config = sim_default_config();
    FILE *trace = tmpfile();
    CHECK(trace != NULL);
    CHECK(load_text("1,0,9,5\n2,1,0,3\n3,2,4,1\n"));
    config.switch_cost = 0;
    config.starvation_threshold = 5;
    CHECK(run_config(config, trace));
    CHECK(expect_trace(trace, expected, sizeof expected / sizeof expected[0]));
    CHECK(fclose(trace) == 0);
    CHECK(result->processes[0].waiting == 0 && result->processes[0].completion == 5);
    CHECK(result->processes[1].waiting == 4 && result->processes[1].completion == 8);
    CHECK(result->processes[2].waiting == 6 && result->processes[2].completion == 9);
    CHECK(result->processes[1].first_run == 5 && result->processes[2].first_run == 8);
    CHECK(result->total_ticks == 9 && result->run_ticks == 9);
    CHECK(result->idle_ticks == 0 && result->switch_ticks == 0);
    CHECK(result->context_switches == 2);
    CHECK(result->max_ready_wait == 6 && result->excessive_wait_processes == 1U);
    CHECK(close_enough(result->average_waiting, 10.0 / 3.0));
    CHECK(close_enough(result->average_turnaround, 19.0 / 3.0));
    CHECK(close_enough(result->average_response, 10.0 / 3.0));
    CHECK(close_enough(result->throughput_per_100_ticks, 300.0 / 9.0));
    CHECK(close_enough(result->useful_cpu_utilization, 1.0));
    CHECK(close_enough(result->jain_fairness, 121.0 / 177.0));
    return true;
}

static bool test_unsorted_arrivals(void)
{
    const ExpectedInterval expected[] = {
        {"RUN", 20, 4}, {"RUN", 30, 1}, {"RUN", 40, 1}, {"RUN", 10, 2}
    };
    SimConfig config = sim_default_config();
    FILE *trace = tmpfile();
    CHECK(trace != NULL);
    CHECK(load_text("10,4,0,2\n20,0,0,4\n30,0,0,1\n40,2,0,1\n"));
    config.switch_cost = 0;
    CHECK(run_config(config, trace));
    CHECK(expect_trace(trace, expected, sizeof expected / sizeof expected[0]));
    CHECK(fclose(trace) == 0);
    CHECK(workload->processes[0].pid == 10); /* The engine must not sort the array. */
    CHECK(result->processes[0].waiting == 2);
    CHECK(result->processes[1].waiting == 0);
    CHECK(result->processes[2].waiting == 4);
    CHECK(result->processes[3].waiting == 3);
    return true;
}

static bool test_priority_independence(void)
{
    size_t i;
    SimConfig config = sim_default_config();
    FILE *first = tmpfile();
    FILE *second = tmpfile();
    CHECK(first != NULL && second != NULL);
    CHECK(load_text("1,0,0,3,2,1\n2,0,9,2\n3,1,4,1\n"));
    config.switch_cost = 1;
    CHECK(run_config(config, first));
    *second_result = *result;
    workload->processes[0].priority = INT_MAX;
    workload->processes[1].priority = 0;
    workload->processes[2].priority = 100;
    CHECK(run_config(config, second));
    CHECK(same_streams(first, second));
    CHECK(result->total_ticks == second_result->total_ticks);
    CHECK(result->context_switches == second_result->context_switches);
    for (i = 0U; i < workload->count; ++i) {
        CHECK(result->processes[i].first_run == second_result->processes[i].first_run);
        CHECK(result->processes[i].completion == second_result->processes[i].completion);
        CHECK(result->processes[i].waiting == second_result->processes[i].waiting);
    }
    CHECK(fclose(first) == 0 && fclose(second) == 0);
    return true;
}

static bool test_single_process_idle_and_io(void)
{
    const ExpectedInterval expected[] = {
        {"IDLE", 0, 2}, {"RUN", 1, 2}, {"IDLE", 0, 3}, {"RUN", 1, 1}
    };
    SimConfig config = sim_default_config();
    FILE *trace = tmpfile();
    CHECK(trace != NULL);
    CHECK(load_text("1,2,0,2,3,1\n"));
    config.switch_cost = 4;
    CHECK(run_config(config, trace));
    CHECK(expect_trace(trace, expected, sizeof expected / sizeof expected[0]));
    CHECK(fclose(trace) == 0);
    CHECK(result->context_switches == 0 && result->switch_ticks == 0);
    CHECK(result->processes[0].waiting == 0 && result->processes[0].first_run == 2);
    CHECK(result->processes[0].cpu_time == 3 && result->processes[0].io_time == 3);
    CHECK(result->processes[0].completion == 8 && result->idle_ticks == 5);
    CHECK(close_enough(result->jain_fairness, 1.0));
    CHECK(close_enough(result->useful_cpu_utilization, 3.0 / 8.0));
    return true;
}

static bool test_blocking_reentry_no_preemption(void)
{
    const ExpectedInterval expected[] = {
        {"RUN", 1, 5}, {"RUN", 2, 1}, {"RUN", 3, 1}, {"RUN", 2, 1}, {"RUN", 1, 1}
    };
    SimConfig config = sim_default_config();
    FILE *trace = tmpfile();
    CHECK(trace != NULL);
    CHECK(load_text("1,0,0,5,2,1\n2,1,0,1,0,1\n3,2,0,1\n"));
    config.switch_cost = 0;
    CHECK(run_config(config, trace));
    CHECK(expect_trace(trace, expected, sizeof expected / sizeof expected[0]));
    CHECK(fclose(trace) == 0);
    CHECK(result->processes[0].waiting == 1);
    CHECK(result->processes[1].waiting == 5);
    CHECK(result->processes[2].waiting == 4);
    CHECK(result->context_switches == 4);
    return true;
}

static bool test_zero_io_simultaneous_events(void)
{
    const ExpectedInterval expected[] = {
        {"RUN", 10, 1}, {"RUN", 40, 1}, {"RUN", 20, 1}, {"RUN", 30, 1}, {"RUN", 10, 1}
    };
    SimConfig config = sim_default_config();
    FILE *trace = tmpfile();
    CHECK(trace != NULL);
    CHECK(load_text("10,0,0,1,0,1\n20,1,0,1\n30,1,0,1\n40,0,0,1\n"));
    config.switch_cost = 0;
    CHECK(run_config(config, trace));
    CHECK(expect_trace(trace, expected, sizeof expected / sizeof expected[0]));
    CHECK(fclose(trace) == 0);
    CHECK(result->processes[0].waiting == 3);
    CHECK(result->processes[1].waiting == 1);
    CHECK(result->processes[2].waiting == 2);
    CHECK(result->processes[3].waiting == 1);
    return true;
}

static bool test_unblock_does_not_preempt(void)
{
    const ExpectedInterval expected[] = {
        {"RUN", 1, 1}, {"RUN", 2, 4}, {"RUN", 1, 1}
    };
    SimConfig config = sim_default_config();
    FILE *trace = tmpfile();
    CHECK(trace != NULL);
    CHECK(load_text("1,0,0,1,1,1\n2,0,0,4\n"));
    config.switch_cost = 0;
    CHECK(run_config(config, trace));
    CHECK(expect_trace(trace, expected, sizeof expected / sizeof expected[0]));
    CHECK(fclose(trace) == 0);
    CHECK(result->processes[0].waiting == 3 && result->processes[0].completion == 6);
    CHECK(result->processes[1].completion == 5);
    return true;
}

static bool test_independent_io_input_order(void)
{
    const ExpectedInterval expected[] = {
        {"RUN", 1, 1}, {"RUN", 2, 1}, {"RUN", 4, 2}, {"RUN", 3, 1}, {"RUN", 1, 1}, {"RUN", 2, 1}
    };
    SimConfig config = sim_default_config();
    FILE *trace = tmpfile();
    CHECK(trace != NULL);
    CHECK(load_text("1,0,0,1,3,1\n2,0,0,1,2,1\n3,4,0,1\n4,0,0,2\n"));
    config.switch_cost = 0;
    CHECK(run_config(config, trace));
    CHECK(expect_trace(trace, expected, sizeof expected / sizeof expected[0]));
    CHECK(fclose(trace) == 0);
    CHECK(result->processes[0].io_time == 3 && result->processes[1].io_time == 2);
    CHECK(result->processes[0].waiting == 1 && result->processes[1].waiting == 3);
    CHECK(result->total_ticks == 7);
    return true;
}

static bool test_context_reservation_and_arrival(void)
{
    const ExpectedInterval expected[] = {
        {"RUN", 1, 1}, {"SWITCH", 2, 2}, {"RUN", 2, 1}, {"SWITCH", 3, 2}, {"RUN", 3, 1}
    };
    SimConfig config = sim_default_config();
    FILE *trace = tmpfile();
    CHECK(trace != NULL);
    CHECK(load_text("1,0,0,1\n2,0,0,1\n3,2,0,1\n"));
    config.switch_cost = 2;
    CHECK(run_config(config, trace));
    CHECK(expect_trace(trace, expected, sizeof expected / sizeof expected[0]));
    CHECK(fclose(trace) == 0);
    CHECK(result->processes[0].first_run == 0 && result->processes[0].waiting == 0);
    CHECK(result->processes[1].first_run == 3 && result->processes[1].waiting == 3);
    CHECK(result->processes[2].first_run == 6 && result->processes[2].waiting == 4);
    CHECK(result->context_switches == 2 && result->switch_ticks == 4);
    CHECK(result->total_ticks == 7 && result->run_ticks == 3);
    CHECK(close_enough(result->average_response, 7.0 / 3.0));
    CHECK(close_enough(result->useful_cpu_utilization, 3.0 / 7.0));
    return true;
}

static bool test_retain_pid_across_idle(void)
{
    const ExpectedInterval expected[] = {
        {"RUN", 1, 1}, {"IDLE", 0, 3}, {"SWITCH", 2, 2}, {"RUN", 2, 1}
    };
    SimConfig config = sim_default_config();
    FILE *trace = tmpfile();
    CHECK(trace != NULL);
    CHECK(load_text("1,0,0,1\n2,4,0,1\n"));
    config.switch_cost = 2;
    CHECK(run_config(config, trace));
    CHECK(expect_trace(trace, expected, sizeof expected / sizeof expected[0]));
    CHECK(fclose(trace) == 0);
    CHECK(result->context_switches == 1 && result->idle_ticks == 3);
    CHECK(result->processes[1].waiting == 2 && result->processes[1].first_run == 6);
    return true;
}

static bool test_continuous_wait_and_strict_threshold(void)
{
    SimConfig config = sim_default_config();
    CHECK(load_text("1,0,0,1,1,1,1,1\n2,0,0,2,0,2\n"));
    config.switch_cost = 0;
    config.starvation_threshold = 1;
    CHECK(run_config(config, NULL));
    CHECK(result->processes[0].waiting == 2 && result->processes[0].max_ready_wait == 1);
    CHECK(result->processes[1].waiting == 2 && result->processes[1].max_ready_wait == 1);
    CHECK(result->excessive_wait_processes == 0U);
    config.starvation_threshold = 0;
    CHECK(run_config(config, NULL));
    CHECK(result->excessive_wait_processes == 2U);
    return true;
}

static bool test_parser_whitespace_and_comments(void)
{
    CHECK(load_text("\n# workload\n  # another comment\r\n 9 , 2 , 3 , 4 , 0 , 1 \r\n\n"));
    CHECK(workload->count == 1U);
    CHECK(workload->processes[0].pid == 9 && workload->processes[0].arrival == 2);
    CHECK(workload->processes[0].priority == 3 && workload->processes[0].burst_count == 3U);
    CHECK(workload->processes[0].bursts[1] == 0);
    return true;
}

static bool test_invalid_inputs(void)
{
    static const char *const bad[] = {
        "", "\n# only a comment\n", "1,0,0\n", "0,0,0,1\n", "-1,0,0,1\n",
        "1,-1,0,1\n", "1,0,-1,1\n", "1,0,0,0\n", "1,0,0,-1\n",
        "1,0,0,1,-1,1\n", "1,0,0,1,2\n", "1,0,0,1,0,0\n",
        "1,0,0,1\n1,1,3,2\n", "pid,arrival,priority,cpu\n", "1,0,0,abc\n",
        "1,0,0,1.5\n", "1,,0,1\n", "1,0,0,1,\n", "1,0,0,1,,2\n",
        "1;0;0;1\n", "1,0,0,99999999999999999999999999\n",
        "99999999999999999999999,0,0,1\n", "1,0,999999999999999999999,1\n",
        "1,0,0,1 junk\n", "1,0,0,1 # inline comments are not CSV fields\n"
    };
    size_t i;
    FILE *input;
    for (i = 0U; i < sizeof bad / sizeof bad[0]; ++i) {
        CHECK(!load_text(bad[i]));
        CHECK(error_text[0] != '\0');
    }
    error_text[0] = '\0';
    CHECK(!workload_load_path("workloads/this_file_does_not_exist.csv", workload,
                              error_text, sizeof error_text));
    CHECK(error_text[0] != '\0');
    input = tmpfile();
    CHECK(input != NULL);
    CHECK(fputs("1,0,0,1", input) != EOF);
    CHECK(fputc('\0', input) != EOF);
    CHECK(fputs("\n", input) != EOF);
    rewind(input);
    CHECK(!workload_load(input, workload, error_text, sizeof error_text));
    CHECK(strstr(error_text, "NUL byte") != NULL);
    CHECK(fclose(input) == 0);
    input = tmpfile();
    CHECK(input != NULL);
    CHECK(fputs("1,0,0,1\n#", input) != EOF);
    for (i = 1U; i < 4096U; ++i) { CHECK(fputc('a', input) != EOF); }
    CHECK(fputc('\n', input) != EOF);
    rewind(input);
    CHECK(workload_load(input, workload, error_text, sizeof error_text));
    CHECK(workload->count == 1U); /* A 4096-character comment is within the bound. */
    CHECK(fseek(input, -1L, SEEK_END) == 0);
    CHECK(fputs("a\n", input) != EOF); /* Now the comment has 4097 characters. */
    rewind(input);
    CHECK(!workload_load(input, workload, error_text, sizeof error_text));
    CHECK(strstr(error_text, "exceeds 4096") != NULL);
    CHECK(fclose(input) == 0);
    error_text[0] = '\0';
    return true;
}

static bool test_input_size_limits(void)
{
    FILE *input = tmpfile();
    SimConfig config = sim_default_config();
    size_t i;
    CHECK(input != NULL);
    for (i = 0U; i < SIM_MAX_PROCESSES; ++i) {
        CHECK(fprintf(input, "%zu,0,0,1\n", i + 1U) > 0);
    }
    rewind(input);
    CHECK(workload_load(input, workload, error_text, sizeof error_text));
    CHECK(workload->count == SIM_MAX_PROCESSES);
    config.switch_cost = 0;
    CHECK(run_config(config, NULL));
    CHECK(result->completed == SIM_MAX_PROCESSES && result->total_ticks == 1024);
    CHECK(fseek(input, 0L, SEEK_END) == 0);
    CHECK(fputs("1025,0,0,1\n", input) != EOF);
    rewind(input);
    CHECK(!workload_load(input, workload, error_text, sizeof error_text));
    CHECK(error_text[0] != '\0');
    CHECK(fclose(input) == 0);
    input = tmpfile();
    CHECK(input != NULL);
    CHECK(fputs("1,0,0", input) != EOF);
    for (i = 0U; i < SIM_MAX_BURSTS; ++i) { CHECK(fputs(",1", input) != EOF); }
    CHECK(fputc('\n', input) != EOF);
    rewind(input);
    CHECK(workload_load(input, workload, error_text, sizeof error_text));
    CHECK(workload->processes[0].burst_count == SIM_MAX_BURSTS);
    CHECK(run_config(config, NULL));
    CHECK(result->completed == 1U && result->total_ticks == 31);
    CHECK(result->run_ticks == 16 && result->idle_ticks == 15);
    CHECK(fclose(input) == 0);
    input = tmpfile();
    CHECK(input != NULL);
    CHECK(fputs("1,0,0", input) != EOF);
    for (i = 0U; i < SIM_MAX_BURSTS + 1U; ++i) { CHECK(fputs(",1", input) != EOF); }
    CHECK(fputc('\n', input) != EOF);
    rewind(input);
    CHECK(!workload_load(input, workload, error_text, sizeof error_text));
    CHECK(error_text[0] != '\0');
    CHECK(fclose(input) == 0);
    input = tmpfile();
    CHECK(input != NULL);
    CHECK(fputs("1,0,0", input) != EOF);
    for (i = 0U; i < SIM_MAX_BURSTS + 2U; ++i) { CHECK(fputs(",1", input) != EOF); }
    CHECK(fputc('\n', input) != EOF);
    rewind(input);
    CHECK(!workload_load(input, workload, error_text, sizeof error_text));
    CHECK(error_text[0] != '\0');
    CHECK(fclose(input) == 0);
    error_text[0] = '\0';
    return true;
}

static bool test_invalid_memory_workloads(void)
{
    CHECK(load_text("1,0,0,1\n"));
    workload->processes[0].bursts[0] = 0;
    CHECK(!workload_validate(workload, error_text, sizeof error_text));
    CHECK(!simulation_run(workload, &(SimConfig){0, 100, 50}, NULL, result,
                           error_text, sizeof error_text));
    workload->processes[0].bursts[0] = 1;
    workload->processes[0].burst_count = SIM_MAX_BURSTS + 1U;
    CHECK(!workload_validate(workload, error_text, sizeof error_text));
    workload->count = SIM_MAX_PROCESSES + 1U;
    CHECK(!workload_validate(workload, error_text, sizeof error_text));
    workload->count = 0U;
    CHECK(!workload_validate(workload, error_text, sizeof error_text));
    error_text[0] = '\0';
    return true;
}

static bool test_invalid_config_and_tick_limit(void)
{
    SimConfig config = sim_default_config();
    CHECK(config.starvation_threshold == 50);
    CHECK(load_text("1,0,0,3\n"));
    config.switch_cost = -1;
    CHECK(!sim_validate_config(&config, error_text, sizeof error_text));
    config = sim_default_config();
    config.starvation_threshold = -1;
    CHECK(!sim_validate_config(&config, error_text, sizeof error_text));
    config.starvation_threshold = SIM_MAX_TIME + 1;
    CHECK(!sim_validate_config(&config, error_text, sizeof error_text));
    config = sim_default_config();
    config.switch_cost = SIM_MAX_TIME + 1;
    CHECK(!sim_validate_config(&config, error_text, sizeof error_text));
    config = sim_default_config();
    config.tick_limit = 0;
    CHECK(!sim_validate_config(&config, error_text, sizeof error_text));
    config.tick_limit = SIM_MAX_TIME + 1;
    CHECK(!sim_validate_config(&config, error_text, sizeof error_text));
    config = sim_default_config();
    config.tick_limit = 2;
    CHECK(!simulation_run(workload, &config, NULL, result, error_text, sizeof error_text));
    CHECK(error_text[0] != '\0');
    config.tick_limit = 3;
    CHECK(run_config(config, NULL)); /* Completion exactly at limit is valid. */
    CHECK(result->total_ticks == 3 && result->completed == 1U);
    CHECK(load_text("1,3,0,1\n"));
    CHECK(!simulation_run(workload, &config, NULL, result, error_text, sizeof error_text));
    CHECK(load_text("1,0,0,1\n2,0,0,1\n"));
    config.switch_cost = 4;
    config.tick_limit = 3;
    CHECK(!simulation_run(workload, &config, NULL, result, error_text, sizeof error_text));
    error_text[0] = '\0';
    return true;
}

static bool test_verify_detects_broken_invariants(void)
{
    SimConfig config = sim_default_config();
    CHECK(load_text("1,0,0,1\n2,1,0,2\n"));
    CHECK(run_config(config, NULL));
    ++result->idle_ticks;
    CHECK(!simulation_verify(workload, result, error_text, sizeof error_text));
    --result->idle_ticks;
    ++result->processes[0].waiting;
    CHECK(!simulation_verify(workload, result, error_text, sizeof error_text));
    --result->processes[0].waiting;
    result->processes[0].state = RUNNING;
    result->processes[1].state = RUNNING;
    CHECK(!simulation_verify(workload, result, error_text, sizeof error_text));
    error_text[0] = '\0';
    return true;
}

static bool write_text_file(const char *path, const char *text)
{
    FILE *output = fopen(path, "wb");
    CHECK(output != NULL);
    CHECK(fputs(text, output) != EOF);
    CHECK(fclose(output) == 0);
    return true;
}

static bool write_cli_input(const char *text)
{
    return write_text_file("build/cli_invalid.csv", text);
}

static bool file_equals_text(const char *path, const char *text)
{
    size_t position = 0U;
    int c;
    FILE *input = fopen(path, "rb");
    CHECK(input != NULL);
    while ((c = fgetc(input)) != EOF) {
        CHECK(text[position] != '\0' && c == (unsigned char)text[position]);
        ++position;
    }
    CHECK(!ferror(input) && text[position] == '\0');
    CHECK(fclose(input) == 0);
    return true;
}

static bool check_cli(FILE *log, const char *arguments,
                      bool expect_success, const char *error_fragment)
{
    char command[1024];
    char captured[2048];
    size_t bytes;
    int status;
    int count;
    FILE *errors;
    const char *program = getenv("FCFS_TEST_PROGRAM");
    if (program == NULL || *program == '\0') {
#ifdef _WIN32
        program = "build\\fcfs.exe";
#else
        program = "./build/fcfs";
#endif
    }
    count = snprintf(command, sizeof command,
                     "%s %s > build/cli.stdout.txt 2> build/cli.stderr.txt",
                     program, arguments);
    CHECK(count > 0 && (size_t)count < sizeof command);
    status = system(command);
    CHECK(status != -1);
    errors = fopen("build/cli.stderr.txt", "rb");
    CHECK(errors != NULL);
    bytes = fread(captured, 1U, sizeof captured - 1U, errors);
    captured[bytes] = '\0';
    CHECK(!ferror(errors));
    CHECK(fclose(errors) == 0);
    CHECK(fprintf(log, "Command: %s %s\nRaw system status: %d\nStderr:\n%s\n",
                  program, arguments, status, captured) > 0);
    if (expect_success) {
        CHECK(status == 0 && bytes == 0U);
    } else {
        CHECK(status != 0 && strstr(captured, "Error:") != NULL);
        CHECK(strstr(captured, error_fragment) != NULL);
    }
    return true;
}

static bool test_cli_exit_status_and_errors(void)
{
    FILE *log = fopen("evidence/cli_checks.txt", "w");
    FILE *output;
    const char *source_text = "1,0,0,2\n";
    CHECK(log != NULL);
    CHECK(write_cli_input("1,0,0,bad\n"));
    CHECK(check_cli(log, "build/cli_invalid.csv", false, "field 4"));
    CHECK(write_cli_input("# empty workload\n\n"));
    CHECK(check_cli(log, "build/cli_invalid.csv", false, "empty"));
    CHECK(write_cli_input("1,0,0,1\n1,2,0,1\n"));
    CHECK(check_cli(log, "build/cli_invalid.csv", false, "duplicate PID"));
    CHECK(write_cli_input("1,0,0,2\n"));
    CHECK(check_cli(log, "build/cli_invalid.csv --tick-limit 0", false, "invalid config"));
    CHECK(check_cli(log, "build/cli_invalid.csv --switch-cost -1", false, "invalid value"));
    CHECK(check_cli(log, "build/cli_invalid.csv --unknown 0", false, "unknown option"));
    CHECK(check_cli(log, "build/cli_invalid.csv --switch-cost", false, "missing value"));
    CHECK(check_cli(log, "build/cli_invalid.csv --tick-limit 1 --trace build/cli.trace.csv --json build/cli.json",
                    false, "tick limit reached"));
    CHECK(check_cli(log, "build/cli_invalid.csv --switch-cost 0 --tick-limit 2 --trace build/cli.trace.csv --json build/cli.json",
                    true, ""));
    output = fopen("build/cli.trace.csv", "rb");
    CHECK(output != NULL && fgetc(output) != EOF);
    CHECK(fclose(output) == 0);
    output = fopen("build/cli.json", "rb");
    CHECK(output != NULL && fgetc(output) == '{');
    CHECK(fclose(output) == 0);
    CHECK(write_text_file("build/cli_input.csv", source_text));
    CHECK(check_cli(log, "build/cli_input.csv --trace build/./cli_input.csv --json build/cli.json",
                    false, "distinct"));
    CHECK(file_equals_text("build/cli_input.csv", source_text));
    CHECK(check_cli(log, "build/cli_input.csv --trace build/cli.trace.csv --json build/./cli_input.csv",
                    false, "distinct"));
    CHECK(file_equals_text("build/cli_input.csv", source_text));
    (void)remove("build/cli_alias.csv");
    CHECK(check_cli(log, "build/cli_input.csv --trace build/cli_alias.csv --json build/./cli_alias.csv",
                    false, "same file"));
    output = fopen("build/cli_alias.csv", "rb");
    CHECK(output != NULL && fgetc(output) == 's'); /* Trace remains; JSON did not overwrite it. */
    CHECK(fclose(output) == 0);
    CHECK(write_text_file("build/cli.trace.csv", "TRACE SENTINEL\n"));
    CHECK(write_text_file("build/cli.json", "JSON SENTINEL\n"));
    CHECK(check_cli(log, "build/cli_input.csv --tick-limit 1 --trace build/cli.trace.csv --json build/cli.json",
                    false, "tick limit reached"));
    CHECK(file_equals_text("build/cli.trace.csv", "TRACE SENTINEL\n"));
    CHECK(file_equals_text("build/cli.json", "JSON SENTINEL\n"));
    CHECK(file_equals_text("build/cli_input.csv", source_text));
    CHECK(check_cli(log, "build/cli_input.csv --trace build/this_directory_does_not_exist/trace.csv --json build/cli.json",
                    false, "cannot open trace"));
    CHECK(file_equals_text("build/cli.json", "JSON SENTINEL\n"));
    CHECK(fclose(log) == 0);
    return true;
}

/* Fixed unsigned arithmetic makes this generator independent of libc rand(). */
static uint32_t random_value(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static bool verify_trace_accounting(FILE *trace)
{
    char line[160];
    char kind[16];
    int64_t start;
    int64_t end;
    int64_t rows = 0;
    int64_t runs = 0;
    int64_t idle = 0;
    int64_t switches = 0;
    rewind(trace);
    CHECK(fgets(line, sizeof line, trace) != NULL);
    CHECK(strcmp(line, "start,end,kind,pid\n") == 0);
    while (fgets(line, sizeof line, trace) != NULL) {
        int pid = 0;
        int fields = sscanf(line, "%" SCNd64 ",%" SCNd64 ",%15[^,],%d",
                            &start, &end, kind, &pid);
        CHECK(fields >= 3 && start == rows && end == rows + 1);
        if (strcmp(kind, "RUN") == 0) {
            CHECK(fields == 4 && pid > 0 && pid <= 256);
            ++runs;
        } else if (strcmp(kind, "SWITCH") == 0) {
            CHECK(fields == 4 && pid > 0 && pid <= 256);
            ++switches;
        } else {
            CHECK(strcmp(kind, "IDLE") == 0 && fields == 3);
            ++idle;
        }
        ++rows; /* Exactly one interval and at most one RUN PID per tick. */
    }
    CHECK(!ferror(trace));
    CHECK(rows == result->total_ticks);
    CHECK(runs == result->run_ticks && idle == result->idle_ticks);
    CHECK(switches == result->switch_ticks && runs + idle + switches == rows);
    return true;
}

static bool test_deterministic_stress(void)
{
    uint32_t seed = UINT32_C(433);
    size_t i;
    SimConfig config = sim_default_config();
    FILE *csv = fopen("workloads/stress_seed_433.csv", "w");
    FILE *first;
    FILE *second;
    FILE *json;
    CHECK(csv != NULL);
    CHECK(fputs("# C xorshift32 seed=433; 256 processes; generated by test_fcfs\n", csv) != EOF);
    for (i = 0U; i < 256U; ++i) {
        uint32_t arrival = random_value(&seed) % UINT32_C(81);
        uint32_t priority = random_value(&seed) % UINT32_C(10);
        uint32_t cpu1 = 1U + random_value(&seed) % UINT32_C(8);
        uint32_t io1 = random_value(&seed) % UINT32_C(7);
        uint32_t cpu2 = 1U + random_value(&seed) % UINT32_C(8);
        uint32_t io2 = random_value(&seed) % UINT32_C(7);
        uint32_t cpu3 = 1U + random_value(&seed) % UINT32_C(8);
        CHECK(fprintf(csv, "%zu,%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32
                      ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "\n",
                      i + 1U, arrival, priority, cpu1, io1, cpu2, io2, cpu3) > 0);
    }
    CHECK(fclose(csv) == 0);
    CHECK(workload_load_path("workloads/stress_seed_433.csv", workload,
                             error_text, sizeof error_text));
    CHECK(workload->count == 256U);
    config.switch_cost = 1;
    config.tick_limit = 100000;
    first = fopen("evidence/stress_a.trace.csv", "w+b");
    second = fopen("evidence/stress_b.trace.csv", "w+b");
    CHECK(first != NULL && second != NULL);
    CHECK(run_config(config, first));
    CHECK(verify_trace_accounting(first));
    *second_result = *result;
    CHECK(run_config(config, second));
    CHECK(verify_trace_accounting(second));
    CHECK(same_streams(first, second));
    CHECK(result->completed == 256U && result->total_ticks <= config.tick_limit);
    CHECK(result->total_ticks == second_result->total_ticks);
    CHECK(result->run_ticks == second_result->run_ticks);
    CHECK(result->switch_ticks == second_result->switch_ticks);
    CHECK(result->context_switches == second_result->context_switches);
    CHECK(result->jain_fairness == second_result->jain_fairness);
    for (i = 0U; i < workload->count; ++i) {
        CHECK(result->processes[i].completion == second_result->processes[i].completion);
        CHECK(result->processes[i].waiting == second_result->processes[i].waiting);
        CHECK(result->processes[i].max_ready_wait == second_result->processes[i].max_ready_wait);
        CHECK(result->processes[i].state == DONE);
    }
    CHECK(fclose(first) == 0 && fclose(second) == 0);
    json = fopen("evidence/stress.json", "w");
    CHECK(json != NULL);
    CHECK(simulation_write_json(json, workload, &config, result));
    CHECK(fclose(json) == 0);
    printf("  Stress seed=433: %zu completed; ticks=%" PRId64
           ", useful=%" PRId64 ", idle=%" PRId64 ", switch=%" PRId64
           "; two traces byte-identical.\n", result->completed, result->total_ticks,
           result->run_ticks, result->idle_ticks, result->switch_ticks);
    return true;
}

typedef bool (*TestFunction)(void);
typedef struct { const char *name; TestFunction run; } TestCase;

int main(void)
{
    static const TestCase tests[] = {
        {"circular FIFO full/empty, wrap-around, callbacks", test_queue},
        {"hand-calculated basic timeline and every metric", test_basic_metrics},
        {"unsorted/equal arrivals and preserved input indices", test_unsorted_arrivals},
        {"priority independence", test_priority_independence},
        {"one process, idle gaps, same PID after I/O", test_single_process_idle_and_io},
        {"blocking/reentry and no arrival or unblock preemption", test_blocking_reentry_no_preemption},
        {"I/O completion cannot preempt a running CPU burst", test_unblock_does_not_preempt},
        {"zero I/O, queued jobs before arrivals before unblocks", test_zero_io_simultaneous_events},
        {"independent I/O, simultaneous completions in input order", test_independent_io_input_order},
        {"positive switch cost, reservation, arrivals during switching", test_context_reservation_and_arrival},
        {"previous PID retained across idle", test_retain_pid_across_idle},
        {"continuous versus total wait, strict threshold", test_continuous_wait_and_strict_threshold},
        {"CSV whitespace, comments and CRLF", test_parser_whitespace_and_comments},
        {"malformed/empty/duplicate/overflow inputs", test_invalid_inputs},
        {"1024-process and 31-burst input bounds", test_input_size_limits},
        {"validation also protects in-memory workloads", test_invalid_memory_workloads},
        {"invalid configuration and exact tick-limit boundary", test_invalid_config_and_tick_limit},
        {"verification rejects damaged invariants", test_verify_detects_broken_invariants},
        {"CLI errors return nonzero status; valid CLI run succeeds", test_cli_exit_status_and_errors},
        {"fixed-seed 256-process stress, two identical traces", test_deterministic_stress}
    };
    size_t i;
    size_t passed = 0U;
    workload = calloc(1U, sizeof *workload);
    result = calloc(1U, sizeof *result);
    second_result = calloc(1U, sizeof *second_result);
    if (workload == NULL || result == NULL || second_result == NULL) {
        fprintf(stderr, "Test allocation failed.\n");
        free(workload); free(result); free(second_result);
        return EXIT_FAILURE;
    }
    for (i = 0U; i < sizeof tests / sizeof tests[0]; ++i) {
        error_text[0] = '\0';
        if (!tests[i].run()) {
            fprintf(stderr, "FAILED: %s\n", tests[i].name);
            break;
        }
        ++passed;
        printf("PASS: %s\n", tests[i].name);
    }
    printf("%zu/%zu test groups passed.\n", passed, sizeof tests / sizeof tests[0]);
    free(workload);
    free(result);
    free(second_result);
    return passed == sizeof tests / sizeof tests[0] ? EXIT_SUCCESS : EXIT_FAILURE;
}
