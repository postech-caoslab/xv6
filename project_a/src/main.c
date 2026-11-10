/* main.c — Homework A tests (instructor supplied; do not edit).
 *
 * trace_spinlock replays trace/spinlock.trace: every role (T0, T1, ...) is a real thread
 * executing its steps in order, `begin-acquire` steps MUST still be spinning after the
 * settle delay, `await-acquire` steps must complete once the holder releases. The xchg
 * choke point (x86.c) verifies the protocol underneath: holder tracking, spin counts,
 * misuse diagnostics on first occurrence, and that every acquisition went through xchg.
 *
 * The concurrent tests hammer mutual exclusion with an occupancy guard and measure what
 * spinning costs: two waiters burn a core each for the whole time the lock is held
 * (lecture 8's motivation for every blocking primitive that follows).
 */
#include "check.h"
#include "course_runtime.h"
#include "instrument.h"
#include "spinlock.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#ifndef HW_IMPL_NAME
#define HW_IMPL_NAME "student"
#endif

#define AWAIT_MS 5000

/* ---- the locks under test ------------------------------------------------------------------ */

static struct spinlock g_spin;

static long g_counter; /* written only while holding the lock under test */
static _Atomic int g_occupancy;
static _Atomic int g_max_occupancy;

static void reset_locks(void)
{
    lockstat_reset();
    initlock(&g_spin, "trace-spin");
    g_counter = 0;
    atomic_store(&g_occupancy, 0);
    atomic_store(&g_max_occupancy, 0);
}

static void note_entered(void)
{
    int occ = atomic_fetch_add(&g_occupancy, 1) + 1;
    int max = atomic_load(&g_max_occupancy);

    while (occ > max && !atomic_compare_exchange_weak(&g_max_occupancy, &max, occ))
        ;
}

static void expect_no_violations(const char *ctx)
{
    CHECK_INVARIANT(lockstat_violations() == 0,
                    "%s: %lu lock-protocol violation(s) at the xchg choke point — see the "
                    "INVARIANT lines above", ctx, lockstat_violations());
}

/* Every acquisition must be an xchg that returned 0: a lock built on anything else (a plain
 * store, a compiler builtin, ...) is invisible to the checker and to the TSan phase. */
static void expect_acquisitions(const char *ctx, unsigned long at_least)
{
    CHECK_INVARIANT(lockstat_acquisitions() >= at_least,
                    "%s: the xchg choke point saw only %lu acquisition(s) where at least %lu "
                    "acquire() calls completed — acquire must take the lock with "
                    "xchg(&lk->locked, 1) and return only once the OLD value was 0",
                    ctx, lockstat_acquisitions(), at_least);
}

/* ---- roles: one persistent thread per trace participant ------------------------------------- */

enum cmd { CMD_NONE, CMD_ACQUIRE, CMD_RELEASE, CMD_INCREMENT, CMD_QUIT };

#define MAX_ROLES 8

typedef struct {
    char name[16];
    pthread_t tid;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    enum cmd cmd;
    _Atomic bool busy; /* a command is posted or executing */
    bool started;
} role_t;

static role_t g_roles[MAX_ROLES];
static size_t g_nroles;

static void *role_main(void *arg)
{
    role_t *r = arg;

    for (;;) {
        enum cmd c;

        pthread_mutex_lock(&r->mu);
        while (r->cmd == CMD_NONE)
            pthread_cond_wait(&r->cv, &r->mu);
        c = r->cmd;
        r->cmd = CMD_NONE;
        pthread_mutex_unlock(&r->mu);

        if (c == CMD_QUIT)
            return NULL;
        switch (c) {
        case CMD_ACQUIRE:
            acquire(&g_spin);
            note_entered();
            CHECK_INVARIANT(g_spin.locked == 1,
                            "%s holds the spinlock but locked == %u (expected 1)",
                            r->name, g_spin.locked);
            break;
        case CMD_RELEASE:
            atomic_fetch_sub(&g_occupancy, 1);
            release(&g_spin);
            break;
        case CMD_INCREMENT:
            g_counter++; /* the trace guarantees this role holds the lock */
            break;
        default:
            break;
        }
        atomic_store(&r->busy, false);
    }
}

static role_t *role_get(const char *name)
{
    for (size_t i = 0; i < g_nroles; i++) {
        if (strcmp(g_roles[i].name, name) == 0)
            return &g_roles[i];
    }
    if (g_nroles == MAX_ROLES)
        hw_die("too many roles in trace");
    role_t *r = &g_roles[g_nroles++];

    snprintf(r->name, sizeof r->name, "%s", name);
    pthread_mutex_init(&r->mu, NULL);
    pthread_cond_init(&r->cv, NULL);
    r->cmd = CMD_NONE;
    atomic_store(&r->busy, false);
    if (pthread_create(&r->tid, NULL, role_main, r) != 0)
        hw_die("cannot create a role thread");
    r->started = true;
    return r;
}

static void role_post(role_t *r, enum cmd c, const char *ctx)
{
    if (atomic_load(&r->busy))
        hw_die("%s: role %s still has an operation in flight", ctx, r->name);
    atomic_store(&r->busy, true);
    pthread_mutex_lock(&r->mu);
    r->cmd = c;
    pthread_cond_signal(&r->cv);
    pthread_mutex_unlock(&r->mu);
}

/* Wait up to ms for the role's last command to finish. */
static bool role_await(role_t *r, unsigned ms)
{
    uint64_t deadline = course_now_ns() + (uint64_t)ms * 1000000ull;

    while (atomic_load(&r->busy)) {
        if (course_now_ns() >= deadline)
            return false;
        course_sleep_ms(1);
    }
    return true;
}

/* Tear down all roles; returns false if some role is stuck (its thread is abandoned). */
static bool roles_teardown(void)
{
    bool clean = true;

    for (size_t i = 0; i < g_nroles; i++) {
        role_t *r = &g_roles[i];

        if (!role_await(r, 100)) {
            clean = false;
            pthread_detach(r->tid);
            continue;
        }
        role_post(r, CMD_QUIT, "teardown");
        pthread_join(r->tid, NULL);
    }
    g_nroles = 0;
    if (!clean) {
        course_line_break_if_open();
        printf("    note: a stuck role thread was abandoned; later tests may cascade\n");
    }
    return clean;
}

/* ---- trace runner ---------------------------------------------------------------------------- */

static bool next_line(FILE *fp, char *buf, size_t bufsz, unsigned *lineno)
{
    while (fgets(buf, (int)bufsz, fp)) {
        (*lineno)++;
        char *hash = strchr(buf, '#');

        if (hash)
            *hash = '\0';
        for (const char *p = buf; *p; p++) {
            if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
                return true;
        }
    }
    return false;
}

static FILE *open_or_die(const char *path)
{
    FILE *fp = fopen(path, "r");

    if (!fp)
        hw_die("cannot open %s — run the tests from the module directory (cd there, or use ./run-all-tests.sh)", path);
    return fp;
}

/* NOLINTBEGIN(cert-err34-c) — trace files are instructor-generated; a non-matching line
 * falls through to hw_die with file:line. */
static void run_trace(const char *name)
{
    const char *acq = "acquire", *beg = "begin-acquire", *awa = "await-acquire";
    const char *rel = "release";
    char path[256], line[256], ctx[300], op[32], who[16];
    unsigned lineno = 0, n_acquires = 0;

    reset_locks();
    snprintf(path, sizeof path, "trace/%s.trace", name);
    FILE *fp = open_or_die(path);
    while (next_line(fp, line, sizeof line, &lineno) && !hw_test_failed()) {
        snprintf(ctx, sizeof ctx, "%s:%u", path, lineno);
        if (sscanf(line, " %31s %15s", op, who) != 2)
            hw_die("%s: unrecognized trace line: %s", ctx, line);
        role_t *r = role_get(who);

        if (strcmp(op, acq) == 0) {
            n_acquires++;
            role_post(r, CMD_ACQUIRE, ctx);
            if (!role_await(r, AWAIT_MS))
                CHECK(false, "%s: %s cannot acquire a FREE lock within %d ms — does a "
                      "previous release() actually store 0?", ctx, r->name, AWAIT_MS);
        } else if (strcmp(op, beg) == 0) {
            n_acquires++;
            role_post(r, CMD_ACQUIRE, ctx);
            course_sleep_ms(course_settle_ms());
            if (!atomic_load(&r->busy))
                CHECK(false, "%s: %s acquired a lock that is HELD — it must keep spinning "
                      "until the holder releases", ctx, r->name);
        } else if (strcmp(op, awa) == 0) {
            if (!role_await(r, AWAIT_MS))
                CHECK(false, "%s: %s is still waiting %d ms after the release — does "
                      "release() store 0?", ctx, r->name, AWAIT_MS);
        } else if (strcmp(op, rel) == 0) {
            role_post(r, CMD_RELEASE, ctx);
            if (!role_await(r, AWAIT_MS))
                CHECK(false, "%s: release by %s did not complete", ctx, r->name);
        } else if (strcmp(op, "increment") == 0) {
            role_post(r, CMD_INCREMENT, ctx);
            if (!role_await(r, AWAIT_MS))
                CHECK(false, "%s: increment by %s did not complete", ctx, r->name);
        } else {
            hw_die("%s: unrecognized op '%s'", ctx, op);
        }
    }
    fclose(fp);
    bool clean = roles_teardown();

    snprintf(path, sizeof path, "trace/%s.answer", name);
    fp = open_or_die(path);
    lineno = 0;
    while (next_line(fp, line, sizeof line, &lineno)) {
        long want;

        snprintf(ctx, sizeof ctx, "%s:%u", path, lineno);
        if (sscanf(line, " counter %ld", &want) == 1) {
            CHECK(g_counter == want, "final counter is %ld, expected %ld — increments ran "
                  "outside mutual exclusion or a role never got the lock", g_counter, want);
        } else if (sscanf(line, " max-occupants %ld", &want) == 1) {
            CHECK(atomic_load(&g_max_occupancy) <= want,
                  "%d threads were inside the critical section at once (expected at most "
                  "%ld) — mutual exclusion is broken", atomic_load(&g_max_occupancy), want);
        } else if (sscanf(line, " state %ld", &want) == 1) {
            if (clean)
                CHECK(g_spin.locked == (uint)want, "final locked state is %u, expected %ld",
                      g_spin.locked, want);
        } else {
            hw_die("%s: unrecognized answer line: %s", ctx, line);
        }
    }
    fclose(fp);
    expect_no_violations(name);
    if (clean)
        expect_acquisitions(name, n_acquires);
}
/* NOLINTEND(cert-err34-c) */

static void test_trace_spinlock(void)
{
    run_trace("spinlock");
}

/* ---- mutual exclusion under load ------------------------------------------------------------ */

typedef struct {
    unsigned iters;
    unsigned long acquired; /* per-worker count: the fairness observation */
} mutex_worker_args_t;

static void *mutex_worker(void *arg)
{
    mutex_worker_args_t *w = arg;

    for (unsigned i = 0; i < w->iters; i++) {
        acquire(&g_spin);
        note_entered();
        g_counter++;
        atomic_fetch_sub(&g_occupancy, 1);
        release(&g_spin);
        w->acquired++;
    }
    return NULL;
}

static void run_mutual_exclusion(unsigned nthreads, unsigned iters)
{
    pthread_t tids[8];
    mutex_worker_args_t args[8];

    reset_locks();
    for (unsigned t = 0; t < nthreads; t++) {
        args[t] = (mutex_worker_args_t){.iters = iters};
        if (pthread_create(&tids[t], NULL, mutex_worker, &args[t]) != 0)
            hw_die("cannot create worker threads");
    }
    unsigned long lo = (unsigned long)-1, hi = 0;

    for (unsigned t = 0; t < nthreads; t++) {
        pthread_join(tids[t], NULL);
        if (args[t].acquired < lo)
            lo = args[t].acquired;
        if (args[t].acquired > hi)
            hi = args[t].acquired;
    }
    CHECK_INVARIANT(atomic_load(&g_max_occupancy) == 1,
                    "%d threads were inside the critical section at once — the lock does "
                    "not provide mutual exclusion", atomic_load(&g_max_occupancy));
    CHECK(g_counter == (long)nthreads * iters,
          "counter is %ld, expected %ld — increments were lost", g_counter,
          (long)nthreads * iters);
    expect_no_violations("mutual exclusion");
    expect_acquisitions("mutual exclusion", (unsigned long)nthreads * iters);
    course_line_break_if_open();
    printf("    observe: per-thread acquisitions min %lu / max %lu — a spinlock promises no "
           "fairness (lecture 8)\n", lo, hi);
}

static void test_mutual_exclusion_spin(void)
{
    run_mutual_exclusion(8, 20000 * hw_stress_scale());
}

/* ---- what spinning costs ------------------------------------------------------------------ */

static _Atomic unsigned g_got_lock;

static void *spin_waiter(void *arg)
{
    (void)arg;
    acquire(&g_spin);
    atomic_fetch_add(&g_got_lock, 1);
    release(&g_spin);
    return NULL;
}

static void test_spin_cpu_cost(void)
{
    enum { WAITERS = 2, HOLD_MS = 300 };
    course_task_t *tasks[WAITERS];

    if (course_ncpus() < 3) {
        course_line_break_if_open();
        printf("    SKIP: needs at least 3 CPUs to show parallel spinning (found %ld)\n",
               course_ncpus());
        return;
    }

    reset_locks();
    atomic_store(&g_got_lock, 0);
    acquire(&g_spin);
    for (unsigned i = 0; i < WAITERS; i++)
        tasks[i] = course_task_start(spin_waiter, NULL);
    course_sleep_ms(course_settle_ms()); /* both waiters are inside acquire() now */

    uint64_t w0 = course_now_ns(), c0 = course_cpu_ns();

    course_sleep_ms(HOLD_MS); /* the holder does NOTHING while they wait */

    double wall_ms = (double)(course_now_ns() - w0) / 1e6;
    double cpu_ms = (double)(course_cpu_ns() - c0) / 1e6;
    unsigned long spins = lockstat_spins();

    release(&g_spin);
    for (unsigned i = 0; i < WAITERS; i++) {
        if (!course_task_await(tasks[i], AWAIT_MS)) {
            CHECK(false, "a waiter never obtained the lock after release");
            course_task_abandon(tasks[i]);
        }
    }
    CHECK(atomic_load(&g_got_lock) == WAITERS, "%u of %d waiters obtained the lock",
          atomic_load(&g_got_lock), WAITERS);
    expect_no_violations("spin cost");
    expect_acquisitions("spin cost", WAITERS + 1);

    course_line_break_if_open();
    printf("    observe: during a %d ms hold with %d waiters the spinlock burned %.0f ms of "
           "CPU (%lu busy exchanges) — a core per waiter, doing nothing useful; this is why "
           "every blocking primitive after this one (Homework B's semaphore) sleeps\n",
           HOLD_MS, WAITERS, cpu_ms, spins);
    CHECK(spins > 0, "no busy exchange was recorded while the lock was held — the waiters "
          "must keep exchanging until xchg(&lk->locked, 1) returns 0");
    CHECK(cpu_ms > 1.3 * wall_ms,
          "expected the waiters to burn CPU (>= 1.3x wall), measured %.0f ms CPU over "
          "%.0f ms wall — does acquire() sleep or yield instead of spinning?", cpu_ms,
          wall_ms);
}

/* ---- runner ---------------------------------------------------------------------------------- */

static const hw_test_t TESTS[] = {
    {"trace_spinlock", test_trace_spinlock,
     "replays trace/spinlock.trace: held lock spins, release hands over, counter/state", 0},
    {"mutual_exclusion_spin", test_mutual_exclusion_spin,
     "8 threads x 20k: occupancy guard, no lost increments, protocol clean at xchg", 0},
    {"spin_cpu_cost", test_spin_cpu_cost,
     "300 ms hold, 2 waiters: the spinlock burns ~2x wall CPU while waiting (lecture 8)", 0},
};

int main(int argc, char **argv)
{
    return hw_run_tests("Homework A (" HW_IMPL_NAME ") — xv6's spinlock from xchg", argc,
                        argv, TESTS, sizeof TESTS / sizeof TESTS[0]);
}
