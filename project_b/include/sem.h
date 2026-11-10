/* sem.h — the course semaphore (instructor supplied; do not edit).
 *
 * Lecture 9's semaphore, with lecture 9's names: sem_init / sem_wait / sem_post. The one
 * difference from POSIX is that sem_init takes the initial value directly (POSIX inserts a
 * `pshared` argument — and its unnamed semaphores do not work on macOS, which is why the
 * course ships its own). Underneath: a counter protected by a pthread mutex + condition
 * variable. sem_wait SLEEPS while the value is 0; it never spins.
 *
 * Every sem_wait/sem_post in this homework flows through these functions, so the harness
 * observes your protocol without touching your code (see instrument.h): waiting on a
 * semaphore while holding the mutex, re-taking the mutex, posting a mutex you do not hold,
 * and wrong initial values are each reported on their FIRST occurrence, with an explanation.
 */
#ifndef COURSE_SEM_H
#define COURSE_SEM_H

#include <pthread.h>
#include <stdbool.h>

struct sem {
    /* All fields are private to src/sem.c — student code uses ONLY the three functions
     * below. Reading `value` from put()/get() would be the exact check-then-act race the
     * semaphore exists to close: the value can change the instant after you look. */
    pthread_mutex_t m;
    pthread_cond_t cv;
    int value;
    unsigned magic;       /* set by sem_init — "was this semaphore ever initialized?" */
    bool student_inited;  /* did bb_init() reach this semaphore? */
    const char *role;     /* instrument.h bookkeeping: "empty" / "full" / "mutex" */
    bool is_mutex;        /* the binary-semaphore-as-lock gets extra protocol checks */
};

void sem_init(struct sem *s, int initial_value); /* initial_value >= 0 */
void sem_wait(struct sem *s); /* sleep while value == 0, then value-- */
void sem_post(struct sem *s); /* value++ and wake one sleeping sem_wait */

#endif /* COURSE_SEM_H */
