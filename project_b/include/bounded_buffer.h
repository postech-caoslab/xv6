/* bounded_buffer.h — Homework B framework (instructor supplied; do not edit).
 *
 * Lecture 9's bounded buffer, with lecture 9's names: a fixed-capacity ring (`fill` is where
 * the next put writes, `use` is where the next get reads) coordinated by THREE semaphores —
 * `empty` counts empty slots, `full` counts filled slots, and `mutex` (a binary semaphore)
 * is the lock around the ring state. The whole lesson of this homework is the ORDER of the
 * sem_wait/sem_post calls around three lines of ring code.
 */
#ifndef COURSE_BOUNDED_BUFFER_H
#define COURSE_BOUNDED_BUFFER_H

#include "course_runtime.h"
#include "sem.h"

#include <stdbool.h>
#include <stddef.h>

struct bounded_buffer {
    item_t *buffer;   /* capacity slots, allocated by the harness                         */
    size_t capacity;
    size_t fill;      /* slot the next put() writes, then advances (wraps % capacity)     */
    size_t use;       /* slot the next get() reads, then advances (wraps % capacity)      */
    struct sem empty; /* counts EMPTY slots:  put() waits on it, get() posts it           */
    struct sem full;  /* counts FILLED slots: get() waits on it, put() posts it           */
    struct sem mutex; /* binary semaphore = the lock around buffer/fill/use (init to 1)   */
};

/* THE student functions (src/bounded_buffer.c). bb_init gives the three semaphores their
 * starting values (the harness has already prepared everything else). put blocks while the
 * buffer is full; get blocks while it is empty. Neither may busy-wait, return early, or
 * touch buffer/fill/use without holding the mutex semaphore. */
void bb_init(struct bounded_buffer *bb);
void put(struct bounded_buffer *bb, item_t item);
item_t get(struct bounded_buffer *bb);

/* ---- instructor harness ---------------------------------------------------------------- */

/* Allocates the ring, declares the semaphore roles to the checker, then calls YOUR
 * bb_init() — and verifies it initialized all three semaphores to the documented values. */
void bb_create(struct bounded_buffer *bb, size_t capacity);
void bb_destroy(struct bounded_buffer *bb);

/* Quiescent-state checks (call ONLY when no put/get is in flight — the tests do): with
 * `filled` items in the buffer, empty must be capacity-filled, full must be `filled`, the
 * mutex must be free (value 1), and fill/use must describe one consistent ring. Prints
 * INVARIANT diagnostics and returns false on any mismatch. */
bool bb_quiescent_ok(struct bounded_buffer *bb, size_t filled);

/* Worker threads and the termination protocol are instructor-owned (students never handle
 * poison). Producer p puts item_make(producer_id, 0..count-1) in order; a consumer gets
 * until it sees a poison item and logs everything else into its exclusively-owned log.
 * After all producers are joined, bb_send_poison() unblocks the consumers. */
typedef struct {
    struct bounded_buffer *bb;
    unsigned producer_id;
    unsigned count;
} bb_producer_args_t;

typedef struct {
    struct bounded_buffer *bb;
    item_log_t log;
} bb_consumer_args_t;

void *bb_producer_worker(void *arg);
void *bb_consumer_worker(void *arg);
void bb_send_poison(struct bounded_buffer *bb, unsigned nconsumers);

#endif /* COURSE_BOUNDED_BUFFER_H */
