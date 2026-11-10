#ifndef FCFS_H
#define FCFS_H

#include <stdbool.h>
#include <stddef.h>

#define FCFS_CAPACITY 1024U

/* Only stable process-array indices are stored in this circular FIFO. */
typedef struct {
    size_t indices[FCFS_CAPACITY];
    size_t head;
    size_t count;
    size_t capacity;
} FcfsQueue;

typedef struct {
    bool (*init)(FcfsQueue *queue, size_t capacity);
    bool (*on_arrival)(FcfsQueue *queue, size_t process_index);
    bool (*on_unblock)(FcfsQueue *queue, size_t process_index);
    bool (*pick_next)(FcfsQueue *queue, size_t *process_index);
    bool (*should_preempt)(const FcfsQueue *queue, size_t running_index);
    void (*cleanup)(FcfsQueue *queue);
} FcfsPolicyOps;

bool fcfs_init(FcfsQueue *queue, size_t capacity);
bool fcfs_enqueue(FcfsQueue *queue, size_t process_index);
bool fcfs_dequeue(FcfsQueue *queue, size_t *process_index);
bool fcfs_should_preempt(const FcfsQueue *queue, size_t running_index);
void fcfs_cleanup(FcfsQueue *queue);
extern const FcfsPolicyOps FCFS_POLICY;

#endif
