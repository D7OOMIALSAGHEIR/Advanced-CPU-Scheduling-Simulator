Advanced CPU Scheduling Simulator
FCFS reference contribution for ICS 433

This package implements only First Come First Served (FCFS). It contains a
standalone tick-based reference engine for demonstrations and an adapter for the
provisional scheduler interface inspected in the confirmed team repository:
https://github.com/D7OOMIALSAGHEIR/Advanced-CPU-Scheduling-Simulator
The inspected branches contained provisional process/scheduler headers but no
shared simulation engine or production main. The reference engine is therefore
kept separate from the adapter. This package does not implement the other
policies or represent completion of the team project.

FILES
  include/fcfs.h       Circular FIFO and FCFS policy operations.
  src/fcfs.c          Policy implementation, separate from simulation mechanics.
  include/simulation.h Shared process, configuration, result, and engine API.
  src/simulation.c    Tick engine, state changes, metrics, trace and JSON output.
  src/workload.c      CSV parsing and workload validation.
  src/main.c          Command-line demonstration program.
  tests/test_fcfs.c   C test harness and deterministic stress generator.
  integration/       Adapter and exact captured provisional header fixtures.
  tests/test_team_adapter.c Adapter tests using the captured headers.
  INTEGRATION.txt    Repository provenance, adapter use, and integration limits.
  workloads/          Three small examples and the generated stress input.
  evidence/           Actual outputs captured during verification.
  EXPLANATION_AR.txt  Simple Arabic explanation and presentation preparation.
  FCFS_PROGRESS.txt   English text for the FCFS part of the progress report.

BUILD AND TEST
Use a C11 compiler and GNU make. The following POSIX commands are provided for
integration convenience; verification was performed on native Windows only.
From this package's root on Linux/macOS:
  make
  make test
  make demo
  make sanitize CC=clang

The strict flags are:
  -std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow

Use CC=gcc or CC=clang to override the compiler. On native Windows with LLVM
MinGW or MinGW-w64 on PATH, use:
  mingw32-make CC=clang
  mingw32-make test CC=clang
  mingw32-make demo CC=clang
  mingw32-make sanitize CC=clang

If GNU make is unavailable, use these direct POSIX commands:
  mkdir -p build
  clang -std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -O2 -Iinclude src/fcfs.c src/simulation.c src/workload.c src/main.c -lm -o build/fcfs
  clang -std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -O2 -Iinclude src/fcfs.c src/simulation.c src/workload.c tests/test_fcfs.c -lm -o build/test_fcfs
  ./build/test_fcfs
Or use these direct commands in Windows PowerShell with clang on PATH:
  New-Item -ItemType Directory -Path build -Force
  clang -std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -O2 -Iinclude src/fcfs.c src/simulation.c src/workload.c src/main.c -lm -o build/fcfs.exe
  clang -std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -O2 -Iinclude src/fcfs.c src/simulation.c src/workload.c tests/test_fcfs.c -lm -o build/test_fcfs.exe
  .\build\test_fcfs.exe
The harness also launches the CLI in build/; build both programs before running
it. Run from the package root so evidence paths exist. FCFS_TEST_PROGRAM can
override the CLI path used by the harness.

Sanitizer support depends on the compiler and runtime. The sanitizer target
instruments both the CLI and the C harness using AddressSanitizer and
UndefinedBehaviorSanitizer, with recovery disabled and frame pointers retained.
It selects the instrumented CLI through FCFS_TEST_PROGRAM. The verified Windows
run used these environment variables in PowerShell before make sanitize:
  $env:ASAN_OPTIONS = 'detect_leaks=0:halt_on_error=1'
  $env:UBSAN_OPTIONS = 'halt_on_error=1:print_stacktrace=1'
Leak detection was disabled; no leak-check or Valgrind result is claimed.
No compiled binaries are included in the ZIP.

RUN
  ./build/fcfs workloads/basic.csv --switch-cost 0 --tick-limit 1000000 --starvation-threshold 50 --trace evidence/basic.trace.csv --json evidence/basic.json
On native Windows, replace ./build/fcfs with .\build\fcfs.exe.

Options:
  --switch-cost N          Nonnegative switch delay; default 1 tick.
  --tick-limit N           Positive maximum elapsed ticks; default 1000000.
  --starvation-threshold N Nonnegative continuous READY wait threshold; default 50.
  --trace PATH             Per-tick CSV output; default trace.csv.
  --json PATH              Metrics JSON output; default results.json.
  --help                   Show usage when used as the only argument.
One tick is one millisecond. Every numeric time/configuration value is bounded
by SIM_MAX_TIME = INT64_MAX / 64 so additions and burst totals cannot overflow.
make demo saves all three console tables to evidence/<name>.console.txt as well
as their CSV traces and JSON results. Read those console files after the target.

The workload, trace, and JSON paths must be distinct. The CLI checks existing
file identity to protect the workload and catch output aliases. This helper uses
Windows file IDs or POSIX stat; the simulator, loader, demo logic, and tests are
C11, and only this file-protection helper calls host-specific APIs. The trace is
staged until parsing and simulation succeed, so parsing and tick-limit failures
preserve existing output files. An output I/O failure can still leave a saved
trace or partial JSON. Always check the exit status; files from a failed command
must not be treated as successful evidence.

INPUT
Each nonblank, noncomment line is one process, in this format without a header:
  pid,arrival,priority,cpu[,io,cpu...]
Example:
  7,3,2,4,0,2
This means PID 7 arrives at t=3 with priority 2, executes 4 CPU ticks, performs
zero I/O ticks, and executes 2 more CPU ticks. Priority is accepted as shared
workload data and is ignored by FCFS.

Leading/trailing whitespace is allowed around fields. Lines whose first
nonwhitespace character is # are comments. Inline comments are not accepted.
PID must be positive and unique (1..INT_MAX). Arrival and priority must be
nonnegative; priority is bounded by INT_MAX. The burst count must be odd and
between 1 and 31. Even positions in the burst sequence are positive CPU times;
odd positions are nonnegative I/O times. At most 1024 processes are accepted.
Each physical input line, including a comment, is limited to 4096 characters.
Malformed fields, missing/extra commas, numeric overflow, duplicate PIDs,
empty input, oversized input, unknown options, and invalid configuration cause
a clear error and a nonzero exit status. Input order is preserved: the loader
does not sort arrivals or use PID/priority to break ties.

POLICY AND READY QUEUE
The policy stores stable process-array indices in a circular queue. Enqueue and
dequeue are O(1); processes are not copied. Arrivals and unblocks join the tail.
pick_next removes the head. The preemption operation always returns false.
A process that starts running keeps the CPU until that CPU burst completes,
even when another process arrives or finishes I/O. The engine scans process
states each tick; this O(n) scan is independent of the O(1) queue operations.

TICK ORDER AND STATES
The states are NEW, READY, RUNNING, BLOCKED, and DONE. At each boundary t:
  1. Commit the CPU burst that completed in [t-1,t). Mark the process DONE,
     or BLOCKED with an I/O completion time of t + its I/O length.
  2. Enqueue arrivals at t, in original input order.
  3. Enqueue I/O completions at t, in original input order.
  4. Dispatch if needed, then execute one RUN, IDLE, or SWITCH interval [t,t+1).
Existing queued processes stay ahead of both groups. Zero-length I/O can return
in step 3 at the same boundary as CPU completion, after all arrivals. Each
process's I/O has an independent completion time; I/O is not serialized and it
continues during idle and context-switch intervals. There is no event heap or
event-time jumping. The engine stops at the boundary where the last process
becomes DONE, without emitting an extra idle tick. A tick-limit error occurs
if the workload needs execution beyond its configured elapsed-time limit.

SHARED CONTEXT-SWITCH CONVENTION
The first dispatch is free and does not increment the switch count. Every later
dispatch to a different PID from the last dispatched PID counts as one switch,
including when the configured cost is zero. A change to another PID still
counts after idle time: the last dispatched PID is retained across idle gaps.
The same PID resuming after its I/O incurs no switch delay and no switch count.
This is a declared measurement convention, not a claim about real CPU hardware.

Once selected, the process is reserved and removed from the FIFO. Arrivals or
unblocks during its switch cannot replace it. It stays in READY until actual
CPU execution begins. Its switch delay therefore contributes to waiting time
and, before its first CPU execution, response time. Other READY processes also
continue waiting during the switch. The RUNNING state begins at useful CPU
execution, so at most one process is RUNNING. These definitions should be agreed
by all team algorithms before comparing their metrics.

METRICS
For a completed process:
  CPU service       Sum of all CPU bursts, equal to useful executed ticks.
  I/O time          Sum of all I/O bursts, including zeros.
  Waiting           Total ticks in READY, including reserved switch delay.
  Turnaround        Completion boundary minus arrival time.
  Response          First actual CPU execution time minus arrival time.
  Max ready wait    Longest uninterrupted READY episode, including switch delay.

Summary definitions:
  Total time        Elapsed ticks from t=0 through the final completion boundary,
                    including initial idle time before the first arrival.
  Throughput        Completed process count * 100 / total time.
  Useful CPU use    RUN ticks / total time, excluding SWITCH ticks from usefulness.
                    JSON stores a fraction; the console may show a percentage.
  Switch count      Dispatches that change PID, except the first dispatch.
  Excessive wait    Number of processes whose max ready wait is STRICTLY greater
                    than the configured threshold (default 50 ticks).
  Jain fairness     x_i = total CPU service_i / turnaround_i;
                    J = (sum x_i)^2 / (n * sum(x_i^2)).
The excessive-wait indicator measures long waits in this finite simulation.
It does not prove indefinite starvation and FCFS fairness is workload dependent.

Required accounting checks are: RUN + IDLE + SWITCH = total elapsed time;
turnaround = CPU service + I/O time + waiting for every completed process;
at most one RUNNING process; all processes complete within the tick limit.
The one-row-per-tick trace distinguishes RUN, IDLE, and SWITCH. Its columns are
start,end,kind,pid. SWITCH identifies the reserved target PID; IDLE has no PID.

INTEGRATING WITH THE TEAM
The confirmed repository was inspected. The provisional Process and Scheduler
headers on feature/sjf-srtf at commit
14f75e3a6c446d8a4c986de3b0a4b60378d1d77a match the header blobs on
feature/round-robin at fea290c13ba8c9c3a859b104c890ed1db9af65e4.
The exact snapshots and provenance are in integration/observed_headers/. They
are fixtures, not newly invented production headers. No shared engine or
production main was present in the inspected main or feature branches.

integration/team_fcfs.c adapts this circular FIFO to those observed Scheduler
callbacks and exports fcfs_scheduler. It binds a stable Process array and queues
its indices, without copying Process structs. The provisional API has no context
argument, so the adapter supports one bound instance at a time. Its void
callbacks report failures through team_fcfs_error(); the engine must check that
status. See INTEGRATION.txt for the lifecycle and exact build commands.

The intended repository location is fcfs/ on feature/fcfs, alongside the other
members' algorithm folders. The source package does not itself establish that
a push or merge has occurred. For team integration, keep the shared engine and
main once they exist; link src/fcfs.c and integration/team_fcfs.c to that engine.
Do not add the reference src/main.c or src/simulation.c to another engine build.
Replace the fixture include path with the finalized shared include path and
recheck callback signatures. Agree on PCB ownership, event order, time units,
context-switch conventions, and metrics before comparing policies.

The policy operations in FCFS_POLICY map to the proposal as follows:
  init          Initialize an empty FIFO once per simulation.
  on_arrival    Enqueue the stable index when NEW becomes READY.
  on_unblock    Enqueue the stable index when BLOCKED becomes READY.
  pick_next     Return/remove the oldest ready index, or report an empty queue.
  should_preempt Always false. Adapt to the team's preemption/on_tick decision.
  cleanup       Reset queue bookkeeping after a run.
FCFS requires no burst prediction, quantum, aging, promotion/demotion, or action
on a CPU tick. A team's on_tick adapter should leave FCFS running. An on_exit
adapter can be a no-op because a dispatched index has already left this queue.
The supplied team adapter translates the queue's indices to pointers into the
bound Process array. Its on_exit callback is a no-op and should_preempt returns
zero. It does not execute CPU ticks, alter bursts, or implement I/O mechanics.
Compatibility targets the captured provisional header signatures; end-to-end
integration with a finalized team engine remains to be tested. Baseline
comparison also needs identical workloads and switch/metric definitions.

VERIFICATION STATUS
Verified on native Windows x86_64 with LLVM-MinGW Clang 23.1.5 and GNU Make 4.4.1.
Both programs compiled under the strict C11 warning flags above. The ordinary
C harness passed 20/20 test groups with exit status 0. Both the harness and CLI
were then compiled with -fsanitize=address,undefined -fno-sanitize-recover=all
-fno-omit-frame-pointer; the instrumented run also passed 20/20 groups with exit
status 0 and no sanitizer diagnostics. Leak detection was disabled, and no
Valgrind run or POSIX runtime test was performed.

The team-interface adapter separately passed 5/5 test groups (4147 checks) in
both strict C11 and ASan/UBSan builds, with exit status 0 and no sanitizer
diagnostics. These tests use the captured provisional headers. They verify FIFO
order, original PCB pointers, capacity and wrap-around, invalid inputs,
duplicate-enqueue rejection, sticky errors, and reset/rebind behavior. Captured
logs are evidence/team_adapter.txt and evidence/team_adapter_sanitizers.txt.

The fixed-seed stress workload (seed 433) completed all 256 processes in 4124
ticks: 3357 RUN + 0 IDLE + 767 SWITCH, with 767 context switches. Its two captured
per-tick traces are byte-identical. Tests check per-process time conservation,
single-process execution, and completion within the configured tick limit.

Captured sample results:
  basic, switch cost 0:        total 9  = 9 RUN + 0 IDLE + 0 SWITCH;
                              average waiting = 11/3 ticks.
  cpu_io, switch cost 1:       total 13 = 9 RUN + 0 IDLE + 4 SWITCH;
                              average waiting = 4 ticks.
  simultaneous, switch cost 1: total 8  = 5 RUN + 0 IDLE + 3 SWITCH;
                              average waiting = 11/3 ticks.

See evidence/tests.txt, evidence/sanitizers.txt, evidence/cli_checks_regular.txt,
and evidence/cli_checks_sanitized.txt for the captured runs. The three
<name>.console.txt,
<name>.trace.csv, and <name>.json files contain actual sample outputs. The
stress_a.trace.csv and stress_b.trace.csv files and stress.json record the stress
case. These outcomes validate this FCFS reference package. Adapter verification
is separate; see INTEGRATION.txt and the logs above. Neither set of tests
establishes operation with a finalized team engine or completion of the project.
