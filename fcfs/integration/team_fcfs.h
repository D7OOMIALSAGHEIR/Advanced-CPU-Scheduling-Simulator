#ifndef TEAM_FCFS_H
#define TEAM_FCFS_H

#include <stdbool.h>
#include <stddef.h>
#include "scheduler.h"

/* Bind borrowed PCBs before init. Their address and input order must stay fixed.
   The observed Scheduler has no context pointer, so this bridge supports one
   active simulation at a time and is not reentrant or thread-safe. */
bool team_fcfs_bind(Process *processes, size_t count);
const char *team_fcfs_error(void); /* NULL on success; errors remain until reset. */
void team_fcfs_cleanup(void);
extern Scheduler fcfs_scheduler;

#endif
