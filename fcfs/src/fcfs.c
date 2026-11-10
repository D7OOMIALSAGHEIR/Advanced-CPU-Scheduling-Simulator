#include "fcfs.h"

static bool queue_is_valid(const FcfsQueue *queue)
{
    return queue != NULL && queue->capacity > 0U &&
           queue->capacity <= FCFS_CAPACITY &&
           queue->head < queue->capacity && queue->count <= queue->capacity;
}

bool fcfs_init(FcfsQueue *queue, size_t capacity)
{
    if (queue == NULL || capacity == 0U || capacity > FCFS_CAPACITY) {
        return false;
    }
    queue->head = 0U;
    queue->count = 0U;
    queue->capacity = capacity;
    return true;
}

bool fcfs_enqueue(FcfsQueue *queue, size_t process_index)
{
    size_t tail;
    if (!queue_is_valid(queue) || queue->count == queue->capacity ||
        process_index >= FCFS_CAPACITY) {
        return false;
    }
    tail = (queue->head + queue->count) % queue->capacity;
    queue->indices[tail] = process_index;
    ++queue->count;
    return true;
}

bool fcfs_dequeue(FcfsQueue *queue, size_t *process_index)
{
    if (!queue_is_valid(queue) || process_index == NULL || queue->count == 0U) {
        return false;
    }
    *process_index = queue->indices[queue->head];
    queue->head = (queue->head + 1U) % queue->capacity;
    --queue->count;
    return true;
}

bool fcfs_should_preempt(const FcfsQueue *queue, size_t running_index)
{
    /* FCFS ignores priority and never interrupts a current CPU burst. */
    (void)queue;
    (void)running_index;
    return false;
}

void fcfs_cleanup(FcfsQueue *queue)
{
    if (queue != NULL) {
        queue->head = 0U;
        queue->count = 0U;
        queue->capacity = 0U;
    }
}

const FcfsPolicyOps FCFS_POLICY = {
    fcfs_init,
    fcfs_enqueue,
    fcfs_enqueue,
    fcfs_dequeue,
    fcfs_should_preempt,
    fcfs_cleanup
};
