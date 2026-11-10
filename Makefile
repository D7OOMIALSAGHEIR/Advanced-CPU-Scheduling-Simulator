# Build and run the SJF / SRTF tests.
#   make        -> build
#   make run    -> build and run all tests
#   make clean  -> delete the program
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -g -Iinclude

test_schedulers: tests/test_schedulers.c src/sjf.c src/srtf.c include/process.h include/scheduler.h
	$(CC) $(CFLAGS) -o test_schedulers tests/test_schedulers.c src/sjf.c src/srtf.c

run: test_schedulers
	./test_schedulers

clean:
	rm -f test_schedulers
