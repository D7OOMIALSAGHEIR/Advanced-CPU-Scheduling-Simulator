/* scheduler.h
 * The interface between the ENGINE and a POLICY (FCFS, SJF, SRTF, RR, ...).
 * The engine calls these functions. The policy only makes decisions.
 * NOTE: names may change when the leader finalizes the real interface.
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "process.h"

typedef struct Scheduler {
    const char *name;
    void     (*init)(void);               /* reset the policy                */
    void     (*on_arrival)(Process *p);   /* a new process arrived           */
    void     (*on_unblock)(Process *p);   /* a process is ready again        */
                                          /* (back from I/O, or preempted)   */
    Process *(*pick_next)(void);          /* who runs next? NULL if nobody   */
    int      (*should_preempt)(Process *running);
                                          /* 1 = stop the running process now*/
    void     (*on_exit)(Process *p);      /* a process finished its burst    */
} Scheduler;

#endif
