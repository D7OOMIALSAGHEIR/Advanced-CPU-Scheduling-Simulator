# Build and run the Priority scheduler tests.
#   make        -> build
#   make run    -> build and run all tests
#   make clean  -> delete the program
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -g -Iinclude

test_priority: tests/test_priority.c src/priority.c include/process.h include/scheduler.h
	$(CC) $(CFLAGS) -o test_priority tests/test_priority.c src/priority.c

run: test_priority
	./test_priority

clean:
	rm -rf test_priority test_priority.dSYM
