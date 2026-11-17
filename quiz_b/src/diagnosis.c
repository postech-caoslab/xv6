/* diagnosis.c — the semaphore-protocol checker behind diagnosis_pre/diagnosis_post
 * (instructor supplied; see instrument.h).
 *
 * The student implements sem_wait/sem_post themselves in this quiz, so the checker cannot
 * live inside them (as it cannot see their bodies). Instead the instructor frames in
 * src/sem_pc.c call diagnosis_pre() before and diagnosis_post() after each student body.
 * That is enough to reconstruct which thread holds the mutex-role semaphore (a wait that
 * returned and has not been posted yet) and to report semaphore-protocol violations on their
 * first occurrence. The bookkeeping needs no extra synchronization: the held-mutex flag is
 * thread-local.
 *
 * Forced interleaving: after every fourth mutex-role sem_post (process-wide count), the
 * posting thread pauses DIAG_POST_DELAY_US before its put()/get() body continues, which
 * widens race windows. Only mutex-role posts outside bb_init are paused, so bare semaphores
 * (sem_basics) and the init phase are unaffected.
 */
#include "instrument.h"
#include "course_runtime.h"
#include "q2.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define MAX_ROLES 8
#define MAX_REPORTS 10
#define DIAG_POST_DELAY_US 100

static struct {
    const struct sem *s;
    const char *role;
    bool is_mutex;
} g_roles[MAX_ROLES];
static size_t g_nroles; /* written only during single-threaded setup, then read-only */

static _Atomic unsigned long g_violations;
static _Atomic unsigned long g_mutex_posts; /* every fourth one pauses, see the header */
static _Atomic bool g_in_bb_init; /* see diag_init_phase() */

/* The one mutex-role semaphore this thread currently holds (NULL = none), plus how many
 * times this thread has taken a mutex — diag_call_begin/end diff the counter to decide
 * whether a put()/get() call ever entered the critical section. */
static _Thread_local const struct sem *t_mutex_held;
static _Thread_local unsigned long t_mutex_acqs;
static _Thread_local unsigned long t_call_start_acqs;
static _Thread_local const char *t_call_fn;
/* The sem_wait/sem_post frame currently running on this thread (NULL outside one): set after
 * diagnosis_pre's checks, cleared at the end of diagnosis_post. diag_where() reads it, with
 * t_call_fn, to say WHERE in the student code a violation happened. */
static _Thread_local const struct sem *t_frame_sem;
static _Thread_local enum diag_op t_frame_op;

void diagnosis_violation(const char *fmt, ...)
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

void diag_reset(void)
{
    /* called between tests, when no student threads are live */
    g_nroles = 0;
    atomic_store(&g_violations, 0);
    atomic_store(&g_mutex_posts, 0);
    atomic_store(&g_in_bb_init, false);
}

void diag_init_phase(bool inside_bb_init)
{
    atomic_store(&g_in_bb_init, inside_bb_init);
}

bool diag_in_init_phase(void)
{
    return atomic_load(&g_in_bb_init);
}

unsigned long diag_violations(void)
{
    return atomic_load(&g_violations);
}

void diag_declare(struct sem *s, const char *role, bool is_mutex)
{
    if (g_nroles == MAX_ROLES) {
        fprintf(stderr, "FATAL: diagnosis: too many declared semaphores\n");
        return;
    }
    g_roles[g_nroles].s = s;
    g_roles[g_nroles].role = role;
    g_roles[g_nroles].is_mutex = is_mutex;
    g_nroles++;
}

/* exposed for q2_harness.c's sem_init: "?" when the semaphore has no declared role */
const char *diag_role_of(const struct sem *s);

static const char *role_of(const struct sem *s)
{
    for (size_t i = 0; i < g_nroles; i++) {
        if (g_roles[i].s == s)
            return g_roles[i].role;
    }
    return "?";
}

static bool is_mutex_role(const struct sem *s)
{
    for (size_t i = 0; i < g_nroles; i++) {
        if (g_roles[i].s == s)
            return g_roles[i].is_mutex;
    }
    return false;
}

const char *diag_role_of(const struct sem *s)
{
    return role_of(s);
}

size_t diag_held_by_me(void)
{
    return t_mutex_held != NULL ? 1 : 0;
}

/* ---- the value, read under the internal lock ----------------------------------------------- */

int diag_sem_value(struct sem *s)
{
    while (xchg(&s->lock.locked, 1) != 0)
        ;

    int v = s->value;

    xchg(&s->lock.locked, 0);
    return v;
}

/* ---- where: the student-code location of the current thread ------------------------------- */

static size_t append(char *buf, size_t n, size_t k, const char *fmt, const char *a, const char *b)
{
    if (k >= n)
        return k;
    int w = snprintf(buf + k, n - k, fmt, a, b);

    return w < 0 ? k : k + (size_t)w;
}

const char *diag_where(char *buf, size_t n, const char *lockfn)
{
    size_t k = append(buf, n, 0, "in %s%s", "", "");
    const char *sep = "";

    if (t_call_fn) {
        k = append(buf, n, k, "%s%s()", sep, t_call_fn);
        sep = " → ";
    }
    if (t_frame_sem) {
        const char *role = role_of(t_frame_sem);
        const char *impl = t_frame_op == DIAG_SEM_WAIT ? "sem_wait_impl" : "sem_post_impl";

        if (strcmp(role, "?") == 0)
            k = append(buf, n, k, "%s%s()", sep, impl);
        else {
            k = append(buf, n, k, "%s%s", sep, impl);
            k = append(buf, n, k, "(&%s)%s", role, "");
        }
        sep = " → ";
    }
    if (lockfn)
        k = append(buf, n, k, "%s%s()", sep, lockfn);
    else if (!t_call_fn && !t_frame_sem)
        append(buf, n, k, "%s%s", "the student code", "");
    return buf;
}

/* Both checkpoints verify that the body released the internal lock; after posts, the value
 * is also read (under the internal lock) and must not be negative. */
static void check_not_negative(struct sem *s, bool read_value)
{
    char where[160];

    if (lockstat_held_by_me(&s->lock.locked)) {
        diagnosis_violation("%s: returned while this thread still holds the semaphore's "
                            "internal lock (s->lock)", diag_where(where, sizeof where, NULL));
        return;
    }
    if (!read_value)
        return;

    int v = diag_sem_value(s);

    if (v < 0)
        diagnosis_violation("%s: after it returned, the semaphore's value is %d — the value "
                            "counts available units and can never be negative",
                            diag_where(where, sizeof where, NULL), v);
}

/* ---- the checkpoints (called by the frames in src/sem_pc.c) -------------------------------- */

void diagnosis_pre(struct sem *s, enum diag_op op)
{
    char where[160];

    if (op == DIAG_SEM_WAIT) {
        if (t_mutex_held == s) {
            diagnosis_violation("%s: sem_wait(&%s) while this thread already holds the %s "
                                "semaphore — only another thread's sem_post(&%s) could let "
                                "it return, and that thread waits for this one (self-deadlock)",
                                diag_where(where, sizeof where, NULL), role_of(s), role_of(s),
                                role_of(s));
        } else if (t_mutex_held != NULL) {
            const char *other = t_call_fn == NULL            ? "the other side"
                                : strcmp(t_call_fn, "get") == 0 ? "a put()"
                                                                : "a get()";

            diagnosis_violation("%s: sem_wait(&%s) while this thread holds the %s semaphore — "
                                "if this sem_wait has to wait, the %s semaphore stays held, "
                                "and %s that would sem_post(&%s) first waits for sem_wait(&%s): "
                                "deadlock",
                                diag_where(where, sizeof where, NULL), role_of(s),
                                role_of(t_mutex_held), role_of(t_mutex_held), other, role_of(s),
                                role_of(t_mutex_held));
        }
    } else { /* DIAG_SEM_POST */
        if (is_mutex_role(s)) {
            if (t_mutex_held == s)
                t_mutex_held = NULL;
            else
                diagnosis_violation("%s: sem_post(&%s) while this thread does not hold the %s "
                                    "semaphore (no matching sem_wait(&%s) on this thread)",
                                    diag_where(where, sizeof where, NULL), role_of(s),
                                    role_of(s), role_of(s));
        }
    }
    t_frame_sem = s;
    t_frame_op = op;
}

void diagnosis_post(struct sem *s, enum diag_op op)
{
    check_not_negative(s, op == DIAG_SEM_POST);
    t_frame_sem = NULL;
    if (op == DIAG_SEM_WAIT) {
        if (is_mutex_role(s) && t_mutex_held == NULL) {
            t_mutex_held = s;
            t_mutex_acqs++;
        }
    } else if (is_mutex_role(s) && !diag_in_init_phase()) {
        /* forced interleaving: see the note at the top of this file */
        if ((atomic_fetch_add(&g_mutex_posts, 1) & 3) == 1)
            course_sleep_us(DIAG_POST_DELAY_US);
    }
}

/* ---- per-call bracket (harness call sites) -------------------------------------------------- */

void diag_call_begin(const char *fn)
{
    t_call_fn = fn;
    t_call_start_acqs = t_mutex_acqs;
}

void diag_call_end(void)
{
    const char *fn = t_call_fn ? t_call_fn : "?";

    if (t_mutex_held != NULL) {
        diagnosis_violation("%s() returned while this thread still holds the %s semaphore — "
                            "every later sem_wait(&%s) by any thread waits forever", fn,
                            role_of(t_mutex_held), role_of(t_mutex_held));
        t_mutex_held = NULL; /* keep later calls on this thread diagnosable */
    }
    if (t_mutex_acqs == t_call_start_acqs)
        diagnosis_violation("%s() returned without taking the mutex semaphore — buffer/fill/use "
                            "were accessed without mutual exclusion, so two threads can use the "
                            "same slot", fn);
    t_call_fn = NULL;
}
