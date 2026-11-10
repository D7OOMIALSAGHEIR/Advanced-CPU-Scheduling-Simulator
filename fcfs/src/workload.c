#include "simulation.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE_LENGTH 4096U

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

bool workload_validate(const Workload *workload, char *error, size_t size)
{
    if (workload == NULL || workload->count == 0U) {
        return fail(error, size, "workload is empty");
    }
    if (workload->count > SIM_MAX_PROCESSES) {
        return fail(error, size, "workload exceeds 1024 processes");
    }
    for (size_t i = 0U; i < workload->count; ++i) {
        const ProcessSpec *p = &workload->processes[i];
        if (p->pid <= 0 || p->priority < 0 || p->arrival < 0 ||
            p->arrival > SIM_MAX_TIME) {
            return fail(error, size, "process %zu has invalid PID, arrival, or priority", i + 1U);
        }
        for (size_t j = 0U; j < i; ++j) {
            if (p->pid == workload->processes[j].pid) {
                return fail(error, size, "duplicate PID %d", p->pid);
            }
        }
        if (p->burst_count == 0U || p->burst_count > SIM_MAX_BURSTS ||
            p->burst_count % 2U == 0U) {
            return fail(error, size, "PID %d requires 1..31 alternating bursts ending in CPU", p->pid);
        }
        for (size_t j = 0U; j < p->burst_count; ++j) {
            if (p->bursts[j] < 0 || p->bursts[j] > SIM_MAX_TIME ||
                (j % 2U == 0U && p->bursts[j] == 0)) {
                return fail(error, size, "PID %d has invalid %s burst %zu", p->pid,
                            j % 2U == 0U ? "CPU" : "I/O", j + 1U);
            }
        }
    }
    return true;
}

/* Accept decimal digits surrounded by whitespace; empty CSV fields stay errors. */
static bool parse_integer(char *field, int64_t maximum, int64_t *value)
{
    while (isspace((unsigned char)*field) != 0) {
        ++field;
    }
    char *end = field + strlen(field);
    while (end > field && isspace((unsigned char)end[-1]) != 0) {
        --end;
    }
    *end = '\0';
    if (*field == '\0') {
        return false;
    }
    for (const char *c = field; *c != '\0'; ++c) {
        if (*c < '0' || *c > '9') {
            return false;
        }
    }
    errno = 0;
    char *number_end = NULL;
    long long parsed = strtoll(field, &number_end, 10);
    if (errno == ERANGE || number_end == field || *number_end != '\0' ||
        parsed < 0 || parsed > maximum) {
        return false;
    }
    *value = (int64_t)parsed;
    return true;
}

static bool parse_row(char *line, Workload *workload, size_t line_number,
                      char *error, size_t size)
{
    char *start = line;
    while (isspace((unsigned char)*start) != 0) {
        ++start;
    }
    if (*start == '\0' || *start == '#') {
        return true;
    }
    if (workload->count == SIM_MAX_PROCESSES) {
        return fail(error, size, "line %zu: workload exceeds 1024 processes", line_number);
    }
    ProcessSpec process = {0};
    size_t fields = 0U;
    for (;;) {
        char *comma = strchr(start, ',');
        if (comma != NULL) {
            *comma = '\0';
        }
        if (fields >= SIM_MAX_BURSTS + 3U) {
            return fail(error, size, "line %zu: more than 31 bursts", line_number);
        }
        int64_t value = 0;
        int64_t maximum = fields == 0U || fields == 2U ? INT_MAX : SIM_MAX_TIME;
        if (!parse_integer(start, maximum, &value)) {
            return fail(error, size, "line %zu field %zu: expected a nonnegative decimal integer in range",
                        line_number, fields + 1U);
        }
        if (fields == 0U) {
            process.pid = (int)value;
        } else if (fields == 1U) {
            process.arrival = value;
        } else if (fields == 2U) {
            process.priority = (int)value;
        } else {
            process.bursts[fields - 3U] = value;
            ++process.burst_count;
        }
        ++fields;
        if (comma == NULL) {
            break;
        }
        start = comma + 1;
    }
    if (fields < 4U || process.pid == 0 || process.burst_count % 2U == 0U) {
        return fail(error, size, "line %zu: expected pid,arrival,priority,cpu[,io,cpu...] with positive PID",
                    line_number);
    }
    for (size_t j = 0U; j < process.burst_count; j += 2U) {
        if (process.bursts[j] == 0) {
            return fail(error, size, "line %zu: CPU bursts must be positive", line_number);
        }
    }
    for (size_t j = 0U; j < workload->count; ++j) {
        if (workload->processes[j].pid == process.pid) {
            return fail(error, size, "line %zu: duplicate PID %d", line_number, process.pid);
        }
    }
    workload->processes[workload->count] = process;
    ++workload->count;
    return true;
}

bool workload_load(FILE *input, Workload *workload, char *error, size_t size)
{
    if (input == NULL || workload == NULL) {
        return fail(error, size, "invalid workload stream or destination");
    }
    workload->count = 0U;
    char line[MAX_LINE_LENGTH + 1U];
    size_t length = 0U;
    size_t line_number = 1U;
    int c;
    /* Reading characters also detects embedded NUL bytes and oversized lines. */
    while ((c = fgetc(input)) != EOF) {
        if (c == '\0') {
            return fail(error, size, "line %zu: NUL byte in CSV", line_number);
        }
        if (c == '\n') {
            line[length] = '\0';
            if (!parse_row(line, workload, line_number, error, size)) {
                return false;
            }
            length = 0U;
            ++line_number;
        } else {
            if (length == MAX_LINE_LENGTH) {
                return fail(error, size, "line %zu exceeds 4096 characters", line_number);
            }
            line[length++] = (char)c;
        }
    }
    if (ferror(input) != 0) {
        return fail(error, size, "error reading workload stream");
    }
    if (length > 0U) {
        line[length] = '\0';
        if (!parse_row(line, workload, line_number, error, size)) {
            return false;
        }
    }
    return workload_validate(workload, error, size);
}

bool workload_load_path(const char *path, Workload *workload, char *error, size_t size)
{
    if (path == NULL) {
        return fail(error, size, "missing workload path");
    }
    FILE *input = fopen(path, "rb");
    if (input == NULL) {
        return fail(error, size, "cannot open workload '%s': %s", path, strerror(errno));
    }
    bool ok = workload_load(input, workload, error, size);
    if (fclose(input) != 0 && ok) {
        ok = fail(error, size, "error closing workload '%s'", path);
    }
    return ok;
}
