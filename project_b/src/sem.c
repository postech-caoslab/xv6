/* sem.c — the course semaphore + the protocol checker behind it (instructor supplied).
 *
 * Every sem_init/sem_wait/sem_post in this homework flows through here, so the harness can
 * check, WITHOUT touching student code:
 *   - waiting on any semaphore while HOLDING the mutex semaphore (the classic
 *     producer/consumer deadlock: sleeping with the lock starves your own waker);
 *   - re-taking the mutex this thread already holds (a semaphore has no owner — the second
 *     wait sleeps forever) and posting a mutex this thread does not hold;
 *   - per put()/get() call (bracketed by the harness): the mutex was taken at least once
 *     and is not still held on return;
 *   - initialization: every semaphore has a defined starting value.
 * Violations are reported on their FIRST occurrence with a teaching diagnostic. The
 * bookkeeping needs no extra synchronization: the held-mutex flag is thread-local, and it
 * is written only by the thread that just took / is about to release that mutex.
 */
#include "instrument.h"
#include "course_runtime.h"
#include "sem.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>

#define SEM_MAGIC 0x53454d21u /* "SEM!" */
#define MAX_REPORTS 10

static _Atomic unsigned long g_violations;
static _Atomic bool g_in_bb_init; /* see semstat_init_phase() */

/* The one mutex-role semaphore this thread currently holds (NULL = none), plus how many
 * times this thread has taken a mutex — semstat_call_begin/end diff the counter to decide
 * whether a put()/get() call ever entered the critical section. */
static _Thread_local struct sem *t_mutex_held;
static _Thread_local unsigned long t_mutex_acqs;
static _Thread_local unsigned long t_call_start_acqs;
static _Thread_local const char *t_call_fn;

void semstat_violation(const char *fmt, ...)
{
    if (atomic_fetch_add(&g_violations, 1) < MAX_REPORTS) {
        va_list ap;

        course_line_break_if_open();
        printf("    INVARIANT: ");
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
        printf("\n");
        fflush(stdout);
    }
}

static const char *role_of(const struct sem *s)
{
    return s->role ? s->role : "?";
}

/* A sem_wait/sem_post on a semaphore that was never initialized would be undefined
 * behavior on garbage pthread objects. Report it once, then give the semaphore a defined
 * (value 0) state so the test can keep running to a diagnosable verdict. */
static void require_inited(struct sem *s, const char *op)
{
    if (s->magic == SEM_MAGIC)
        return;
    semstat_violation("%s on a semaphore that was never sem_init'ed — bb_init() must give "
                      "all three semaphores a starting value before any put()/get() runs",
                      op);
    sem_prepare(s, "?", false);
}

void sem_prepare(struct sem *s, const char *role, bool is_mutex)
{
    pthread_mutex_init(&s->m, NULL);
    pthread_cond_init(&s->cv, NULL);
    s->value = 0;
    s->magic = SEM_MAGIC;
    s->student_inited = false;
    s->role = role;
    s->is_mutex = is_mutex;
}

void sem_init(struct sem *s, int initial_value)
{
    if (initial_value < 0) {
        semstat_violation("sem_init(&%s, %d): a semaphore's value counts a resource and can "
                          "never be negative — using 0 instead", role_of(s), initial_value);
        initial_value = 0;
    }
    if (s->magic == SEM_MAGIC) {
        /* Prepared by bb_create (the normal path): keep the live pthread objects and the
         * declared role; just install the student's starting value. */
        if (s->role && !atomic_load(&g_in_bb_init))
            semstat_violation("sem_init(&bb->%s, %d) called OUTSIDE bb_init() — a running "
                              "buffer's semaphores must never be re-initialized: it resets "
                              "the count (or frees a mutex another thread holds) and the "
                              "lecture's invariants no longer hold", role_of(s),
                              initial_value);
        pthread_mutex_lock(&s->m);
        s->value = initial_value;
        s->student_inited = true;
        pthread_mutex_unlock(&s->m);
        return;
    }
    sem_prepare(s, "?", false);
    s->value = initial_value;
    s->student_inited = true;
}

void sem_wait(struct sem *s)
{
    require_inited(s, "sem_wait");

    if (t_mutex_held == s) {
        semstat_violation("sem_wait on the %s semaphore this thread ALREADY holds — a "
                          "semaphore has no owner and cannot be re-entered: this wait can "
                          "only return if some OTHER thread posts it (self-deadlock)",
                          role_of(s));
    } else if (t_mutex_held != NULL) {
        semstat_violation("sem_wait(&%s) while HOLDING the %s semaphore — if this thread "
                          "has to sleep here, it sleeps WITH the lock, and the thread that "
                          "would sem_post(&%s) blocks on sem_wait(&%s) first: deadlock "
                          "(lecture 9's wrong order). Take the mutex AFTER this wait.",
                          role_of(s), role_of(t_mutex_held), role_of(s),
                          role_of(t_mutex_held));
    }

    pthread_mutex_lock(&s->m);
    while (s->value == 0)
        pthread_cond_wait(&s->cv, &s->m);
    s->value--;
    pthread_mutex_unlock(&s->m);

    if (s->is_mutex && t_mutex_held == NULL) {
        t_mutex_held = s;
        t_mutex_acqs++;
    }
}

void sem_post(struct sem *s)
{
    require_inited(s, "sem_post");

    if (s->is_mutex) {
        if (t_mutex_held == s)
            t_mutex_held = NULL;
        else
            semstat_violation("sem_post on the %s semaphore this thread does NOT hold — a "
                              "binary semaphore used as a lock must be posted by the thread "
                              "inside the critical section, exactly once per wait",
                              role_of(s));
    }

    pthread_mutex_lock(&s->m);
    s->value++;
    pthread_cond_signal(&s->cv);
    pthread_mutex_unlock(&s->m);
}

/* ---- introspection ------------------------------------------------------------------------ */

void semstat_reset(void)
{
    /* called between tests, when no student threads are live */
    atomic_store(&g_violations, 0);
    atomic_store(&g_in_bb_init, false);
}

void semstat_init_phase(bool inside_bb_init)
{
    atomic_store(&g_in_bb_init, inside_bb_init);
}

unsigned long semstat_violations(void)
{
    return atomic_load(&g_violations);
}

int semstat_value(struct sem *s)
{
    if (s->magic != SEM_MAGIC)
        return -1;
    pthread_mutex_lock(&s->m);
    int v = s->value;

    pthread_mutex_unlock(&s->m);
    return v;
}

void semstat_call_begin(const char *fn)
{
    t_call_fn = fn;
    t_call_start_acqs = t_mutex_acqs;
}

void semstat_call_end(void)
{
    const char *fn = t_call_fn ? t_call_fn : "?";

    if (t_mutex_held != NULL) {
        semstat_violation("%s() returned while still HOLDING the %s semaphore — every path "
                          "out of %s() must sem_post it, or the next thread to take the "
                          "mutex waits forever", fn, role_of(t_mutex_held), fn);
        t_mutex_held = NULL; /* keep later calls on this thread diagnosable */
    }
    if (t_mutex_acqs == t_call_start_acqs)
        semstat_violation("%s() completed without EVER taking the mutex semaphore — "
                          "buffer/fill/use were read and advanced with no mutual exclusion, "
                          "so two threads can use the same slot (lecture 9's fill race)",
                          fn);
    t_call_fn = NULL;
}
