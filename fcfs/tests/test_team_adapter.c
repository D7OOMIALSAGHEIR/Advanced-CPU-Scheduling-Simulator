#include "team_fcfs.h"
#include "fcfs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t checks;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        return false; \
    } \
} while (0)

static bool test_fifo_and_original_pcbs(void)
{
    Process processes[4] = {{0}};
    for (size_t i = 0U; i < 4U; ++i) {
        processes[i].pid = (int)i + 1;
        processes[i].next = &processes[(i + 1U) % 4U];
    }
    CHECK(team_fcfs_bind(processes, 4U));
    fcfs_scheduler.init();
    CHECK(strcmp(fcfs_scheduler.name, "FCFS") == 0);
    CHECK(team_fcfs_error() == NULL);
    fcfs_scheduler.on_arrival(&processes[2]);
    fcfs_scheduler.on_arrival(&processes[0]);
    fcfs_scheduler.on_unblock(&processes[1]);
    /* Burst estimates, remaining time, PID, and arrival cannot reorder FIFO. */
    processes[2].remaining = 999;
    processes[0].remaining = 1;
    processes[2].burst = 1000;
    processes[0].burst = 1;
    processes[2].arrival = 100;
    processes[0].arrival = 0;
    CHECK(fcfs_scheduler.pick_next() == &processes[2]);
    fcfs_scheduler.on_exit(&processes[2]);
    CHECK(fcfs_scheduler.pick_next() == &processes[0]);
    fcfs_scheduler.on_unblock(&processes[2]);
    CHECK(fcfs_scheduler.pick_next() == &processes[1]);
    CHECK(fcfs_scheduler.pick_next() == &processes[2]);
    CHECK(fcfs_scheduler.pick_next() == NULL);
    CHECK(team_fcfs_error() == NULL);
    for (size_t i = 0U; i < 4U; ++i) {
        CHECK(processes[i].next == &processes[(i + 1U) % 4U]);
    }
    CHECK(processes[2].remaining == 999 && processes[0].remaining == 1);
    CHECK(fcfs_scheduler.should_preempt(&processes[2]) == 0);
    CHECK(fcfs_scheduler.should_preempt(NULL) == 0);
    return true;
}

static bool test_full_capacity_and_wrap(void)
{
    Process *processes = calloc(FCFS_CAPACITY, sizeof *processes);
    CHECK(processes != NULL);
    CHECK(team_fcfs_bind(processes, FCFS_CAPACITY));
    for (size_t i = 0U; i < FCFS_CAPACITY; ++i) {
        processes[i].next = &processes[i];
        fcfs_scheduler.on_arrival(&processes[i]);
        CHECK(team_fcfs_error() == NULL);
    }
    for (size_t i = 0U; i < FCFS_CAPACITY / 2U; ++i) {
        CHECK(fcfs_scheduler.pick_next() == &processes[i]);
    }
    for (size_t i = 0U; i < FCFS_CAPACITY / 2U; ++i) {
        fcfs_scheduler.on_unblock(&processes[i]);
        CHECK(team_fcfs_error() == NULL);
    }
    for (size_t i = FCFS_CAPACITY / 2U; i < FCFS_CAPACITY; ++i) {
        CHECK(fcfs_scheduler.pick_next() == &processes[i]);
    }
    for (size_t i = 0U; i < FCFS_CAPACITY / 2U; ++i) {
        CHECK(fcfs_scheduler.pick_next() == &processes[i]);
    }
    CHECK(fcfs_scheduler.pick_next() == NULL && team_fcfs_error() == NULL);
    for (size_t i = 0U; i < FCFS_CAPACITY; ++i) {
        CHECK(processes[i].next == &processes[i]);
    }
    team_fcfs_cleanup();
    free(processes);
    return true;
}

static bool test_invalid_binding_and_pointers(void)
{
    Process processes[2] = {{0}};
    Process unrelated = {0};
    CHECK(!team_fcfs_bind(NULL, 1U));
    CHECK(team_fcfs_error() != NULL);
    CHECK(!team_fcfs_bind(processes, 0U));
    CHECK(!team_fcfs_bind(processes, FCFS_CAPACITY + 1U));
    CHECK(team_fcfs_bind(processes, 2U));
    fcfs_scheduler.on_arrival(NULL);
    CHECK(team_fcfs_error() != NULL);
    CHECK(fcfs_scheduler.pick_next() == NULL);
    fcfs_scheduler.init();
    CHECK(team_fcfs_error() == NULL);
    fcfs_scheduler.on_unblock(&unrelated);
    CHECK(team_fcfs_error() != NULL);
    fcfs_scheduler.init();
    /* One-past is valid to form but is not a PCB in the bound array. */
    fcfs_scheduler.on_arrival(processes + 2);
    CHECK(team_fcfs_error() != NULL);
    fcfs_scheduler.init();
    /* Integer-to-pointer conversion is used only to test rejected addresses
       on the same flat-address platforms supported by the adapter. */
    Process *misaligned = (Process *)((uintptr_t)(void *)processes + 1U);
    fcfs_scheduler.on_arrival(misaligned);
    CHECK(team_fcfs_error() != NULL);
    CHECK(fcfs_scheduler.pick_next() == NULL);
    return true;
}

static bool test_sticky_error_and_reset(void)
{
    Process processes[2] = {{0}};
    CHECK(team_fcfs_bind(processes, 2U));
    fcfs_scheduler.on_arrival(&processes[0]);
    fcfs_scheduler.on_unblock(&processes[0]);
    const char *first_error = team_fcfs_error();
    CHECK(first_error != NULL && strstr(first_error, "already") != NULL);
    fcfs_scheduler.on_arrival(&processes[1]);
    CHECK(team_fcfs_error() == first_error);
    CHECK(fcfs_scheduler.pick_next() == NULL);
    fcfs_scheduler.on_exit(NULL);
    CHECK(team_fcfs_error() == first_error);
    CHECK(fcfs_scheduler.should_preempt(&processes[0]) == 0);
    fcfs_scheduler.init();
    CHECK(team_fcfs_error() == NULL);
    CHECK(fcfs_scheduler.pick_next() == NULL);
    fcfs_scheduler.on_unblock(&processes[0]);
    CHECK(fcfs_scheduler.pick_next() == &processes[0]);
    CHECK(team_fcfs_error() == NULL);
    return true;
}

static bool test_cleanup_and_rebind(void)
{
    Process old_array[2] = {{0}};
    Process new_array[1] = {{0}};
    team_fcfs_cleanup();
    CHECK(team_fcfs_error() == NULL);
    CHECK(fcfs_scheduler.pick_next() == NULL);
    CHECK(team_fcfs_error() != NULL);
    team_fcfs_cleanup();
    fcfs_scheduler.init();
    CHECK(team_fcfs_error() != NULL);
    CHECK(team_fcfs_bind(old_array, 2U));
    fcfs_scheduler.on_arrival(&old_array[1]);
    CHECK(team_fcfs_bind(new_array, 1U));
    CHECK(fcfs_scheduler.pick_next() == NULL && team_fcfs_error() == NULL);
    fcfs_scheduler.on_arrival(&new_array[0]);
    CHECK(fcfs_scheduler.pick_next() == &new_array[0]);
    fcfs_scheduler.on_arrival(&old_array[0]);
    CHECK(team_fcfs_error() != NULL);
    team_fcfs_cleanup();
    CHECK(team_fcfs_error() == NULL);
    return true;
}

int main(void)
{
    static const struct { const char *name; bool (*run)(void); } tests[] = {
        {"FIFO, original PCB pointers, field independence, no preemption", test_fifo_and_original_pcbs},
        {"1024-process capacity, FIFO wrap, no Process.next changes", test_full_capacity_and_wrap},
        {"invalid bindings and callback pointers", test_invalid_binding_and_pointers},
        {"duplicate detection, sticky errors, init reset", test_sticky_error_and_reset},
        {"cleanup, unbound callbacks, rebind", test_cleanup_and_rebind}
    };
    size_t passed = 0U;
    for (size_t i = 0U; i < sizeof tests / sizeof tests[0]; ++i) {
        if (!tests[i].run()) { break; }
        ++passed;
        printf("PASS: %s\n", tests[i].name);
    }
    team_fcfs_cleanup();
    printf("%zu/%zu team adapter groups passed; %zu checks.\n",
           passed, sizeof tests / sizeof tests[0], checks);
    return passed == sizeof tests / sizeof tests[0] ? EXIT_SUCCESS : EXIT_FAILURE;
}
