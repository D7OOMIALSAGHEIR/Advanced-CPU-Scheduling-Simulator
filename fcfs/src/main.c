#include "simulation.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#endif

/* File identity catches aliases such as ./input.csv, links, and Windows case
   differences. Only this CLI file-protection helper uses host-specific APIs;
   the scheduling, loader, and metrics use the C11 library. */
static bool same_existing_file(const char *first, const char *second)
{
#ifdef _WIN32
    DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE a = CreateFileA(first, 0, sharing, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE b = CreateFileA(second, 0, sharing, NULL, OPEN_EXISTING, 0, NULL);
    BY_HANDLE_FILE_INFORMATION info_a;
    BY_HANDLE_FILE_INFORMATION info_b;
    bool same = false;
    if (a != INVALID_HANDLE_VALUE && b != INVALID_HANDLE_VALUE &&
        GetFileInformationByHandle(a, &info_a) != 0 &&
        GetFileInformationByHandle(b, &info_b) != 0) {
        same = info_a.dwVolumeSerialNumber == info_b.dwVolumeSerialNumber &&
               info_a.nFileIndexHigh == info_b.nFileIndexHigh &&
               info_a.nFileIndexLow == info_b.nFileIndexLow;
    }
    if (a != INVALID_HANDLE_VALUE) { (void)CloseHandle(a); }
    if (b != INVALID_HANDLE_VALUE) { (void)CloseHandle(b); }
    return same;
#else
    struct stat a;
    struct stat b;
    return stat(first, &a) == 0 && stat(second, &b) == 0 &&
           a.st_dev == b.st_dev && a.st_ino == b.st_ino;
#endif
}

static void usage(FILE *output, const char *program)
{
    (void)fprintf(output, "Usage: %s WORKLOAD.csv [options]\n"
                  "  --switch-cost N           Context-switch ticks (default 1)\n"
                  "  --tick-limit N            Maximum executed ticks (default 1000000)\n"
                  "  --starvation-threshold N  Excessive continuous wait (default 50)\n"
                  "  --trace PATH              Per-tick CSV output (default trace.csv)\n"
                  "  --json PATH               Metrics JSON output (default results.json)\n"
                  "  --help                    Show this help\n", program);
}

static bool parse_option_number(const char *text, int64_t *value)
{
    if (*text == '\0') {
        return false;
    }
    for (const char *c = text; *c != '\0'; ++c) {
        if (*c < '0' || *c > '9') {
            return false;
        }
    }
    errno = 0;
    char *end = NULL;
    long long number = strtoll(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || number < 0 || number > SIM_MAX_TIME) {
        return false;
    }
    *value = (int64_t)number;
    return true;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(stdout, argv[0]);
        return EXIT_SUCCESS;
    }
    if (argc < 2 || argv[1][0] == '-') {
        usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }
    const char *input_path = argv[1];
    const char *trace_path = "trace.csv";
    const char *json_path = "results.json";
    SimConfig config = sim_default_config();
    for (int i = 2; i < argc; i += 2) {
        if (i + 1 >= argc) {
            (void)fprintf(stderr, "Error: missing value for '%s'\n", argv[i]);
            return EXIT_FAILURE;
        }
        if (strcmp(argv[i], "--trace") == 0) {
            trace_path = argv[i + 1];
        } else if (strcmp(argv[i], "--json") == 0) {
            json_path = argv[i + 1];
        } else {
            int64_t *destination = NULL;
            if (strcmp(argv[i], "--switch-cost") == 0) {
                destination = &config.switch_cost;
            } else if (strcmp(argv[i], "--tick-limit") == 0) {
                destination = &config.tick_limit;
            } else if (strcmp(argv[i], "--starvation-threshold") == 0) {
                destination = &config.starvation_threshold;
            } else {
                (void)fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
                return EXIT_FAILURE;
            }
            if (!parse_option_number(argv[i + 1], destination)) {
                (void)fprintf(stderr, "Error: invalid value '%s' for %s (range 0..%" PRId64 ")\n",
                              argv[i + 1], argv[i], SIM_MAX_TIME);
                return EXIT_FAILURE;
            }
        }
    }
    char error[SIM_ERROR_SIZE];
    if (!sim_validate_config(&config, error, sizeof(error))) {
        (void)fprintf(stderr, "Error: %s\n", error);
        return EXIT_FAILURE;
    }
    /* Refuse accidental overwrites before opening output files. */
    if (*trace_path == '\0' || *json_path == '\0' ||
        strcmp(input_path, trace_path) == 0 || strcmp(input_path, json_path) == 0 ||
        strcmp(trace_path, json_path) == 0 ||
        same_existing_file(input_path, trace_path) ||
        same_existing_file(input_path, json_path) ||
        same_existing_file(trace_path, json_path)) {
        (void)fprintf(stderr, "Error: workload, trace, and JSON paths must be nonempty and distinct\n");
        return EXIT_FAILURE;
    }
    Workload *workload = malloc(sizeof(*workload));
    SimResult *result = malloc(sizeof(*result));
    if (workload == NULL || result == NULL) {
        (void)fprintf(stderr, "Error: out of memory\n");
        free(workload);
        free(result);
        return EXIT_FAILURE;
    }
    FILE *trace = NULL;
    FILE *json = NULL;
    int status = EXIT_FAILURE;
    if (!workload_load_path(input_path, workload, error, sizeof(error))) {
        (void)fprintf(stderr, "Error: %s\n", error);
        goto cleanup;
    }
    /* Stage the trace until the entire run succeeds, so failures do not publish
       partial traces under a success-looking filename. */
    trace = tmpfile();
    if (trace == NULL) {
        (void)fprintf(stderr, "Error: cannot create temporary trace stream\n");
        goto cleanup;
    }
    if (!simulation_run(workload, &config, trace, result, error, sizeof(error))) {
        (void)fprintf(stderr, "Error: %s\n", error);
        goto cleanup;
    }
    FILE *saved_trace = fopen(trace_path, "wb");
    if (saved_trace == NULL) {
        (void)fprintf(stderr, "Error: cannot open trace '%s': %s\n", trace_path, strerror(errno));
        goto cleanup;
    }
    rewind(trace);
    unsigned char buffer[4096];
    size_t bytes;
    bool copy_ok = true;
    while ((bytes = fread(buffer, 1U, sizeof(buffer), trace)) > 0U) {
        if (fwrite(buffer, 1U, bytes, saved_trace) != bytes) {
            copy_ok = false;
            break;
        }
    }
    if (ferror(trace) != 0) {
        copy_ok = false;
    }
    if (fclose(saved_trace) != 0) {
        copy_ok = false;
    }
    if (!copy_ok) {
        (void)fprintf(stderr, "Error: cannot write trace '%s'\n", trace_path);
        goto cleanup;
    }
    /* The trace may have just created a previously nonexistent alias of JSON. */
    if (same_existing_file(trace_path, json_path)) {
        (void)fprintf(stderr, "Error: trace and JSON paths identify the same file\n");
        goto cleanup;
    }
    json = fopen(json_path, "wb");
    if (json == NULL) {
        (void)fprintf(stderr, "Error: cannot open JSON '%s': %s\n", json_path, strerror(errno));
        goto cleanup;
    }
    if (!simulation_write_json(json, workload, &config, result)) {
        (void)fprintf(stderr, "Error: cannot write JSON '%s'\n", json_path);
        goto cleanup;
    }
    if (fclose(json) != 0) {
        json = NULL;
        (void)fprintf(stderr, "Error: cannot close JSON '%s'\n", json_path);
        goto cleanup;
    }
    json = NULL;
    simulation_print(stdout, workload, &config, result);
    if (fflush(stdout) != 0 || ferror(stdout) != 0) {
        (void)fprintf(stderr, "Error: cannot write console output\n");
        goto cleanup;
    }
    status = EXIT_SUCCESS;
cleanup:
    if (trace != NULL) {
        (void)fclose(trace);
    }
    if (json != NULL) {
        (void)fclose(json);
    }
    free(result);
    free(workload);
    return status;
}
