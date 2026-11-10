/* process.h
 * A very simple Process struct.
 * NOTE: the team's real struct will have more fields (bursts[], ...).
 * When the leader pushes the real one, just replace this file.
 *
 * Same as the struct used by the FCFS, SJF/SRTF and RR branches, plus the
 * "priority" field, which the Priority scheduler needs. The proposal's
 * Process struct already lists a priority field, so the real one will have it.
 */
#ifndef PROCESS_H
#define PROCESS_H

typedef struct Process {
    int pid;         /* process id                                  */
    int arrival;     /* time the process arrives                    */
    int priority;    /* SMALLER number = HIGHER priority (0 = top)  */
    int burst;       /* total length of the CPU burst               */
    int remaining;   /* how much of the burst is still left to run  */
    int first_run;   /* first time on CPU (-1 = never ran yet)      */
    int finish;      /* time the process finished                   */
    struct Process *next;  /* used by the ready list (linked list)  */
} Process;

#endif
