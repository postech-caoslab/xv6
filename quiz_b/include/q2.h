/* q2.h — Quiz Q2 framework (instructor supplied; do not edit).
 *
 * You build the whole coordination stack, bottom-up, from ONE hardware primitive:
 *
 *     xchg  →  spinlock  →  counting semaphore  →  bounded buffer (producer/consumer)
 *
 * The semaphore here SPINS: with only xchg there is nothing to sleep on (no OS sleep/wakeup
 * or park facility, and pthread calls are forbidden). Its value is a counter protected by a
 * small internal spinlock.
 *
 * The bounded buffer is a fixed ring (`fill` is where the next put writes, `use` is where
 * the next get reads) coordinated by THREE semaphores: `empty` counts empty slots, `full`
 * counts filled slots, and `mutex` (a binary semaphore) is the lock around the ring state.
 */
#ifndef COURSE_Q2_H
#define COURSE_Q2_H

#include "course_runtime.h"
#include "spinlock.h"
#include "x86.h"

#include <stdbool.h>
#include <stddef.h>

/* ---- layer 2: the counting semaphore you implement --------------------------------------- */

struct sem {
    struct spinlock lock; /* internal lock protecting `value`                              */
    int value;            /* units currently AVAILABLE: never negative                     */
};

/* Provided (src/q2_harness.c): value = initial_value (>= 0; a
 * negative value is reported and treated as 0), internal lock word = 0. */
void sem_init(struct sem *s, int initial_value);

/* YOU implement the bodies (src/sem_pc.c: sem_wait_impl / sem_post_impl).
 *   sem_wait: returns only after taking one unit (value - 1); while value is 0 it waits
 *             (spinning) for a sem_post.
 *   sem_post: adds one unit (value + 1). Never blocks. */
void sem_wait(struct sem *s);
void sem_post(struct sem *s);

/* ---- layer 3: the bounded buffer you implement on top of YOUR semaphore ------------------- */

struct bounded_buffer {
    item_t *buffer;   /* capacity slots, allocated by the harness                         */
    size_t capacity;
    size_t fill;      /* slot the next put() writes (0 .. capacity-1)                     */
    size_t use;       /* slot the next get() reads (0 .. capacity-1)                      */
    struct sem empty; /* counts EMPTY slots                                               */
    struct sem full;  /* counts FILLED slots                                              */
    struct sem mutex; /* binary semaphore: the lock that protects buffer/fill/use        */
};

/* YOU implement (src/sem_pc.c). bb_init gives the three semaphores their starting values
 * (the harness has already prepared everything else). put blocks while the buffer is full;
 * get blocks while it is empty. Neither may touch buffer/fill/use without holding the
 * mutex semaphore. */
void bb_init(struct bounded_buffer *bb);
void put(struct bounded_buffer *bb, item_t item);
item_t get(struct bounded_buffer *bb);

/* ---- instructor harness ------------------------------------------------------------------ */

/* Allocates the ring, declares the semaphore roles to the checker, then calls YOUR
 * bb_init() — and checks the starting values bb_init() gave the three semaphores. */
void bb_create(struct bounded_buffer *bb, size_t capacity);
void bb_destroy(struct bounded_buffer *bb);

/* Quiescent-state checks (call ONLY when no put/get is in flight — the tests do): compares
 * the semaphore values, internal lock words and fill/use with a buffer holding `filled`
 * items. Prints INVARIANT diagnostics and returns false on any mismatch. */
bool bb_quiescent_ok(struct bounded_buffer *bb, size_t filled);

/* Worker threads and the termination protocol are instructor-owned (students never handle
 * poison). Producer p puts item_make(producer_id, 0..count-1) in order; a consumer gets
 * until it sees a poison item and logs everything else into its exclusively-owned log. */
typedef struct {
    struct bounded_buffer *bb;
    unsigned producer_id;
    unsigned count;
} q2_producer_args_t;

typedef struct {
    struct bounded_buffer *bb;
    item_log_t log;
} q2_consumer_args_t;

void *q2_producer_worker(void *arg);
void *q2_consumer_worker(void *arg);
void q2_send_poison(struct bounded_buffer *bb, unsigned nconsumers);

#endif /* COURSE_Q2_H */
