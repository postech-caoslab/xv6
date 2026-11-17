/* sem_pc.c — Quiz Q2: YOUR file. Implement the seven marked regions; edit nothing else.
 *
 * Allowed inside the regions: xchg() on lk->locked; your acquire()/release() (Stage 1) on a
 * semaphore's internal lock; sem_wait/sem_post on the buffer's three semaphores (sem_init
 * only inside bb_init(), once per semaphore); and the ring fields (buffer, capacity, fill,
 * use). Forbidden: pthread calls, condition variables, <stdatomic.h>, sleeping, and calling
 * the diagnosis_* checkpoints yourself.
 */
#include "instrument.h"
#include "q2.h"

#include <stdlib.h>

/* ---- layer 1: the spinlock, from xchg ------------------------------------------------------ */

void acquire(struct spinlock *lk)
{
    (void)lk;

    /* === BEGIN STUDENT CODE: DO NOT EDIT OUTSIDE THIS REGION === */

    abort(); /* fails loudly on purpose — REMOVE this line after implementing your code */

    /* === END STUDENT CODE === */
}

void release(struct spinlock *lk)
{
    (void)lk;

    /* === BEGIN STUDENT CODE: DO NOT EDIT OUTSIDE THIS REGION === */

    abort(); /* fails loudly on purpose — REMOVE this line after implementing your code */

    /* === END STUDENT CODE === */
}

/* ---- layer 2: the counting semaphore, from your spinlock -----------------------------------
 * sem_wait/sem_post below are instructor FRAMES: the diagnosis_pre()/diagnosis_post() calls
 * are REQUIRED for grading — they power the checker that explains your mistakes. Do not
 * remove or edit them, and never call them from your own code. You implement the *_impl
 * bodies; returning from inside your region is fine (the frame still runs its checkpoint). */

static void sem_wait_impl(struct sem *s);
static void sem_post_impl(struct sem *s);

void sem_wait(struct sem *s)
{
    diagnosis_pre(s, DIAG_SEM_WAIT); /* required for checking — do not remove */
    sem_wait_impl(s);
    diagnosis_post(s, DIAG_SEM_WAIT); /* required for checking — do not remove */
}

void sem_post(struct sem *s)
{
    diagnosis_pre(s, DIAG_SEM_POST); /* required for checking — do not remove */
    sem_post_impl(s);
    diagnosis_post(s, DIAG_SEM_POST); /* required for checking — do not remove */
}

static void sem_wait_impl(struct sem *s)
{
    (void)s;

    /* === BEGIN STUDENT CODE: DO NOT EDIT OUTSIDE THIS REGION === */

    abort(); /* fails loudly on purpose — REMOVE this line after implementing your code */

    /* === END STUDENT CODE === */
}

static void sem_post_impl(struct sem *s)
{
    (void)s;

    /* === BEGIN STUDENT CODE: DO NOT EDIT OUTSIDE THIS REGION === */

    abort(); /* fails loudly on purpose — REMOVE this line after implementing your code */

    /* === END STUDENT CODE === */
}

/* ---- layer 3: the bounded buffer, from your semaphore -------------------------------------- */

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
