/* bounded_buffer.c — Homework B: YOUR file. Implement the three functions; edit nothing else.
 *
 * Allowed inside the regions: the three semaphores (bb->empty, bb->full, bb->mutex) through
 * sem_init/sem_wait/sem_post, and the ring fields (buffer, capacity, fill, use). Forbidden:
 * pthread mutexes/condvars, atomics, extra synchronization objects, busy-waiting, sleeping,
 * and reading a semaphore's fields directly (sem.h is the whole interface).
 */
#include "bounded_buffer.h"

#include <stdlib.h>

void bb_init(struct bounded_buffer *bb)
{
    (void)bb;

    /* === BEGIN STUDENT CODE: DO NOT EDIT OUTSIDE THIS REGION === */

    abort(); /* fails loudly on purpose — REMOVE this line after implementing your code */

    /* === END STUDENT CODE === */
}

void put(struct bounded_buffer *bb, item_t item)
{
    (void)bb;
    (void)item;

    /* === BEGIN STUDENT CODE: DO NOT EDIT OUTSIDE THIS REGION === */

    abort(); /* fails loudly on purpose — REMOVE this line after implementing your code */

    /* === END STUDENT CODE === */
}

item_t get(struct bounded_buffer *bb)
{
    (void)bb;

    /* === BEGIN STUDENT CODE: DO NOT EDIT OUTSIDE THIS REGION === */

    abort(); /* fails loudly on purpose — REMOVE this line after implementing your code */

    /* === END STUDENT CODE === */
}
