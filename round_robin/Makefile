# Build and run the Round Robin tests.
#   make        -> build
#   make run    -> build and run all tests
#   make clean  -> delete the program
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -g -Iinclude

test_rr: tests/test_rr.c src/rr.c include/process.h include/scheduler.h
	$(CC) $(CFLAGS) -o test_rr tests/test_rr.c src/rr.c

run: test_rr
	./test_rr

clean:
	rm -rf test_rr test_rr.dSYM
