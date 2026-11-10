CC = cc
CPPFLAGS = -Iinclude
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -O2
LDLIBS = -lm
SANFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all

CORE = src/fcfs.c src/simulation.c src/workload.c
HEADERS = include/fcfs.h include/simulation.h
ADAPTER = src/fcfs.c integration/team_fcfs.c
ADAPTER_HEADERS = include/fcfs.h integration/team_fcfs.h integration/observed_headers/process.h integration/observed_headers/scheduler.h
ADAPTER_CPPFLAGS = -Iinclude -Iintegration -Iintegration/observed_headers

ifeq ($(OS),Windows_NT)
SHELL := cmd.exe
.SHELLFLAGS := /C
EXEEXT = .exe
MKBUILD = if not exist build mkdir build
RUN_MAIN = build\fcfs.exe
RUN_TEST = build\test_fcfs.exe
RUN_ADAPTER = build\test_team_adapter.exe
RUN_ADAPTER_SANITIZE = build\test_team_adapter_sanitize.exe
RUN_SANITIZE = set FCFS_TEST_PROGRAM=build\fcfs_sanitize.exe&& build\test_fcfs_sanitize.exe
else
EXEEXT =
MKBUILD = mkdir -p build
RUN_MAIN = ./build/fcfs
RUN_TEST = ./build/test_fcfs
RUN_ADAPTER = ./build/test_team_adapter
RUN_ADAPTER_SANITIZE = ./build/test_team_adapter_sanitize
RUN_SANITIZE = FCFS_TEST_PROGRAM=./build/fcfs_sanitize ./build/test_fcfs_sanitize
endif

.PHONY: all test test-adapter demo sanitize sanitize-adapter clean

all: build/fcfs$(EXEEXT)

build:
	$(MKBUILD)

build/fcfs$(EXEEXT): $(CORE) src/main.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) src/main.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_fcfs$(EXEEXT): $(CORE) tests/test_fcfs.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/test_fcfs.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_team_adapter$(EXEEXT): $(ADAPTER) tests/test_team_adapter.c $(ADAPTER_HEADERS) | build
	$(CC) $(ADAPTER_CPPFLAGS) $(CFLAGS) $(ADAPTER) tests/test_team_adapter.c $(LDFLAGS) -o $@

test: all build/test_fcfs$(EXEEXT) build/test_team_adapter$(EXEEXT)
	$(RUN_TEST)
	$(RUN_ADAPTER)

test-adapter: build/test_team_adapter$(EXEEXT)
	$(RUN_ADAPTER)

demo: build/fcfs$(EXEEXT)
	$(RUN_MAIN) workloads/basic.csv --switch-cost 0 --trace evidence/basic.trace.csv --json evidence/basic.json > evidence/basic.console.txt
	$(RUN_MAIN) workloads/cpu_io.csv --switch-cost 1 --trace evidence/cpu_io.trace.csv --json evidence/cpu_io.json > evidence/cpu_io.console.txt
	$(RUN_MAIN) workloads/simultaneous.csv --switch-cost 1 --trace evidence/simultaneous.trace.csv --json evidence/simultaneous.json > evidence/simultaneous.console.txt

build/test_fcfs_sanitize$(EXEEXT): $(CORE) tests/test_fcfs.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(SANFLAGS) $(CORE) tests/test_fcfs.c $(LDFLAGS) $(LDLIBS) -o $@

build/fcfs_sanitize$(EXEEXT): $(CORE) src/main.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(SANFLAGS) $(CORE) src/main.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_team_adapter_sanitize$(EXEEXT): $(ADAPTER) tests/test_team_adapter.c $(ADAPTER_HEADERS) | build
	$(CC) $(ADAPTER_CPPFLAGS) $(SANFLAGS) $(ADAPTER) tests/test_team_adapter.c $(LDFLAGS) -o $@

sanitize: all build/fcfs_sanitize$(EXEEXT) build/test_fcfs_sanitize$(EXEEXT) build/test_team_adapter_sanitize$(EXEEXT)
	$(RUN_SANITIZE)
	$(RUN_ADAPTER_SANITIZE)

sanitize-adapter: build/test_team_adapter_sanitize$(EXEEXT)
	$(RUN_ADAPTER_SANITIZE)

clean:
ifeq ($(OS),Windows_NT)
	if exist build\fcfs.exe del /Q build\fcfs.exe
	if exist build\test_fcfs.exe del /Q build\test_fcfs.exe
	if exist build\test_fcfs_sanitize.exe del /Q build\test_fcfs_sanitize.exe
	if exist build\fcfs_sanitize.exe del /Q build\fcfs_sanitize.exe
	if exist build\test_team_adapter.exe del /Q build\test_team_adapter.exe
	if exist build\test_team_adapter_sanitize.exe del /Q build\test_team_adapter_sanitize.exe
else
	rm -f build/fcfs build/test_fcfs build/test_fcfs_sanitize build/fcfs_sanitize build/test_team_adapter build/test_team_adapter_sanitize
endif
