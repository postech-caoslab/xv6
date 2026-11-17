/* main.c — Quiz Q2 visible tests (instructor supplied; do not edit).
 *
 * spinlock_basics and sem_basics drive the spinlock and the semaphore directly.
 * trace_session replays trace/session.trace against the full stack: begin-* steps must
 * block, await-* steps must finish once the trace unblocks them, and `sems E F` checkpoints
 * compare the counting semaphores' values at quiescent points. The fleet tests check exact
 * FIFO (1P/1C), per-producer order (MPSC) and exactly-once delivery (MPMC) over per-consumer
 * logs. The xchg checker (x86.c) and the semaphore checker (diagnosis.c) report protocol
 * violations on their first occurrence.
 */
#include "check.h"
#include "course_runtime.h"
#include "instrument.h"
#include "q2.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#ifndef HW_IMPL_NAME
#define HW_IMPL_NAME "student"
#endif

#define AWAIT_MS (COURSE_SANITIZED ? 15000 : 5000)

static void reset_checkers(void)
{
    lockstat_reset();
    diag_reset();
}

static void expect_no_violations(const char *ctx)
{
    CHECK_INVARIANT(lockstat_violations() == 0,
                    "%s: %lu spinlock-protocol violation(s) — see the INVARIANT lines above", ctx, lockstat_violations());
    CHECK_INVARIANT(diag_violations() == 0,
                    "%s: %lu semaphore-protocol violation(s) — see the INVARIANT lines above", ctx, diag_violations());
}

/* ---- spinlock_basics: your spinlock alone, before anything sits on top of it --------------- */

static struct spinlock g_lk; /* static: a failed run may abandon a waiter spinning on it */

static void *spinlock_waiter_thunk(void *arg)
{
    (void)arg;
    acquire(&g_lk);
    release(&g_lk);
    return NULL;
}

static void test_spinlock_basics(void)
{
    reset_checkers();
    initlock(&g_lk, "basics");

    acquire(&g_lk);
    CHECK(g_lk.locked == 1, "after acquire() on a free lock, lk->locked is %u, expected 1",
          g_lk.locked);
    CHECK_INVARIANT(lockstat_held_by_me(&g_lk.locked),
                    "acquire() returned, but none of this thread's xchg() calls on lk->locked "
                    "took the lock (returned old value 0)");
    release(&g_lk);
    CHECK(g_lk.locked == 0, "after release(), lk->locked is %u, expected 0", g_lk.locked);
    if (hw_test_failed())
        return;

    /* A held lock must make the second thread wait, and release must hand it over. */
    acquire(&g_lk);

    course_task_t *waiter = course_task_start(spinlock_waiter_thunk, NULL);

    course_sleep_ms(course_settle_ms());
    if (course_task_done(waiter)) {
        CHECK(false, "acquire() returned while another thread held the lock");
        course_task_await(waiter, AWAIT_MS);
        return;
    }
    CHECK(lockstat_spins() > 0, "the thread waiting in acquire() made no failed xchg(&lk->locked, 1) calls "
          "while the lock was held");
    release(&g_lk);
    if (!course_task_await(waiter, AWAIT_MS)) {
        CHECK(false, "acquire() in the waiting thread did not return after the holder called "
              "release()");
        course_task_abandon(waiter);
        return;
    }
    CHECK(g_lk.locked == 0, "lk->locked is %u after the waiter's release(), expected 0",
          g_lk.locked);
    CHECK(lockstat_acquisitions() == 3, "%lu xchg() calls took the lock, expected 3 "
          "(two acquire() calls by one thread, one by the waiter)", lockstat_acquisitions());
    expect_no_violations("spinlock_basics");
}

/* ---- sem_basics: your semaphore alone, before the buffer sits on top of it ----------------- */

static struct sem g_sem; /* static: a failed run may abandon a waiter blocked on it */

static void *sem_wait_thunk(void *arg)
{
    (void)arg;
    sem_wait(&g_sem);
    return NULL;
}

static void *sem_post_thunk(void *arg)
{
    (void)arg;
    sem_post(&g_sem);
    return NULL;
}

static void test_sem_basics(void)
{
    reset_checkers();
    sem_init(&g_sem, 2);

    for (int i = 0; i < 2; i++) {
        course_task_t *t = course_task_start(sem_wait_thunk, NULL);

        if (!course_task_await(t, AWAIT_MS)) {
            CHECK(false, "sem_wait() #%d on value %d did not return within %d ms", i + 1, 2 - i,
                  AWAIT_MS);
            course_task_abandon(t);
            return;
        }
    }
    CHECK(g_sem.value == 0, "after two sem_wait() calls on an initial value of 2, value is %d, "
          "expected 0", g_sem.value);
    CHECK_INVARIANT(g_sem.lock.locked == 0,
                    "s->lock.locked is still 1 after sem_wait() returned");

    /* value is 0 now: the next wait must block — WITHOUT sitting on the internal lock. */
    course_task_t *waiter = course_task_start(sem_wait_thunk, NULL);

    course_sleep_ms(course_settle_ms());
    if (course_task_done(waiter)) {
        CHECK(false, "sem_wait() on value 0 returned without any sem_post()");
    } else {
        /* Probe: a sem_post from another thread while a sem_wait waits on value 0. The bounded
         * await turns a hang into a diagnosis. */
        course_task_t *poster = course_task_start(sem_post_thunk, NULL);

        if (!course_task_await(poster, AWAIT_MS)) {
            CHECK(false, "sem_post() did not return within %d ms while another thread was waiting "
                  "in sem_wait() on value 0",
                  AWAIT_MS);
            course_task_abandon(poster);
            course_task_abandon(waiter);
            return;
        }
        if (!course_task_await(waiter, AWAIT_MS)) {
            CHECK(false, "a sem_wait() waiting on value 0 did not return within %d ms after a "
                  "sem_post()", AWAIT_MS);
            course_task_abandon(waiter);
            return;
        }
        CHECK(g_sem.value == 0, "after sem_post() (+1) and the woken sem_wait() (-1), value is %d, "
              "expected 0", g_sem.value);
    }

    sem_post(&g_sem);
    CHECK(g_sem.value == 1, "sem_post() with nobody waiting left value %d, expected 1", g_sem.value);
    CHECK_INVARIANT(g_sem.lock.locked == 0,
                    "s->lock.locked is still 1 after sem_post() returned");
    if (hw_test_failed())
        return;

    /* Two waiters, one post: exactly one waiter may return. */
    sem_wait(&g_sem); /* take the unit back: value 0, nobody waiting */

    course_task_t *w1 = course_task_start(sem_wait_thunk, NULL);
    course_task_t *w2 = course_task_start(sem_wait_thunk, NULL);

    course_sleep_ms(course_settle_ms());

    int v = diag_sem_value(&g_sem);

    CHECK(v == 0, "with two threads waiting in sem_wait() on value 0, the value is %d, "
          "expected 0", v);
    CHECK(!course_task_done(w1) && !course_task_done(w2),
          "a sem_wait() on value 0 returned without any sem_post()");

    course_task_t *poster = course_task_start(sem_post_thunk, NULL);

    if (!course_task_await(poster, AWAIT_MS)) {
        CHECK(false, "sem_post() did not return within %d ms while two threads were waiting in "
              "sem_wait()", AWAIT_MS);
        course_task_abandon(poster);
        course_task_abandon(w1);
        course_task_abandon(w2);
        return;
    }
    /* A correct waiter need not finish within one settle sleep under instrumentation.
     * Observe eventual completion without joining/freeing either task yet. */
    uint64_t release_deadline = course_now_ns() + (uint64_t)AWAIT_MS * 1000000ull;
    while (!course_task_done(w1) && !course_task_done(w2) &&
           course_now_ns() < release_deadline)
        course_sleep_ms(1);
    course_sleep_ms(course_settle_ms()); /* also expose an erroneous second completion */

    int released = (course_task_done(w1) ? 1 : 0) + (course_task_done(w2) ? 1 : 0);

    CHECK(released == 1, "one sem_post() with two threads waiting in sem_wait() let %d of them "
          "return, expected exactly 1",
          released);
    sem_post(&g_sem); /* and the second post releases the other one */
    bool w1_done = course_task_await(w1, AWAIT_MS);
    bool w2_done = course_task_await(w2, AWAIT_MS);
    if (!w1_done || !w2_done) {
        CHECK(false, "the second sem_post() did not let the remaining sem_wait() return within %d ms",
              AWAIT_MS);
        if (!w1_done)
            course_task_abandon(w1);
        if (!w2_done)
            course_task_abandon(w2);
        return;
    }
    CHECK(g_sem.value == 0, "after two sem_wait() and two sem_post() calls the value is %d, expected 0",
          g_sem.value);
    CHECK_INVARIANT(g_sem.lock.locked == 0,
                    "s->lock.locked is still 1 after the two-waiter round");
    expect_no_violations("sem_basics");
}

/* ---- put/get on task threads, bracketed by the per-call protocol window --------------------- */

typedef struct {
    struct bounded_buffer *bb;
    item_t item; /* put: the item to insert; get: the result */
} op_arg_t;

static item_log_t g_consumed; /* appended by get ops in completion order */

static void *put_thunk(void *arg)
{
    op_arg_t *op = arg;

    diag_call_begin("put");
    put(op->bb, op->item);
    diag_call_end();
    return NULL;
}

static void *get_thunk(void *arg)
{
    op_arg_t *op = arg;

    diag_call_begin("get");
    op->item = get(op->bb);
    diag_call_end();
    item_log_append(&g_consumed, op->item);
    return NULL;
}

/* ---- trace roles: P0, P1, ... produce; C0 consumes ------------------------------------------- */

#define MAX_ROLES 8

typedef struct {
    char name[16];
    course_task_t *task; /* a begun, not yet awaited operation */
    op_arg_t arg;
} role_t;

static role_t g_roles[MAX_ROLES];
static size_t g_nroles;

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
    r->task = NULL;
    return r;
}

/* "A0" -> item_make('A'-'A', 0). The letter is the producer, the number the sequence. */
static bool parse_item(const char *tok, item_t *out)
{
    if (tok[0] < 'A' || tok[0] > 'Z')
        return false;
    unsigned seq = 0;

    for (const char *p = tok + 1; *p; p++) {
        if (*p < '0' || *p > '9')
            return false;
        seq = seq * 10 + (unsigned)(*p - '0');
    }
    *out = item_make((unsigned)(tok[0] - 'A'), seq);
    return true;
}

#define ITEM_STR_LEN 32

static const char *item_str(item_t it, char buf[ITEM_STR_LEN])
{
    if (item_is_poison(it))
        snprintf(buf, ITEM_STR_LEN, "<poison>");
    else if (it.producer_id < 26)
        snprintf(buf, ITEM_STR_LEN, "%c%u", 'A' + (int)it.producer_id, it.sequence);
    else
        snprintf(buf, ITEM_STR_LEN, "(p%u,#%u)", it.producer_id, it.sequence);
    return buf;
}

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

/* Start op on the role's thread; if must_block, it has to still be running after settle;
 * otherwise it must complete within AWAIT_MS. */
static void begin_op(struct bounded_buffer *bb, role_t *r, bool is_put, item_t item,
                     bool must_block, const char *ctx)
{
    if (r->task)
        hw_die("%s: role %s already has an operation in flight", ctx, r->name);
    r->arg.bb = bb;
    r->arg.item = item;
    r->task = course_task_start(is_put ? put_thunk : get_thunk, &r->arg);
    if (must_block) {
        course_sleep_ms(course_settle_ms());
        if (course_task_done(r->task)) {
            CHECK(false, "%s: %s() by %s returned although the buffer is %s at this point", ctx,
                  is_put ? "put" : "get", r->name, is_put ? "full" : "empty");
            course_task_await(r->task, AWAIT_MS);
            r->task = NULL;
        }
    }
}

static void await_op(role_t *r, bool is_put, const item_t *expect, bool was_blocked,
                     const char *ctx)
{
    char b1[ITEM_STR_LEN], b2[ITEM_STR_LEN];

    if (!r->task)
        hw_die("%s: role %s has no operation in flight to await", ctx, r->name);
    if (!course_task_await(r->task, AWAIT_MS)) {
        if (was_blocked)
            CHECK(false, "%s: %s() by %s is still blocked %d ms after a %s() made %s available",
                  ctx, is_put ? "put" : "get", r->name, AWAIT_MS, is_put ? "get" : "put",
                  is_put ? "a free slot" : "an item");
        else
            CHECK(false, "%s: %s() by %s did not return within %d ms although the buffer "
                  "had %s", ctx, is_put ? "put" : "get", r->name, AWAIT_MS,
                  is_put ? "a free slot" : "an item");
        course_task_abandon(r->task);
        r->task = NULL;
        return;
    }
    r->task = NULL;
    if (expect) {
        CHECK(r->arg.item.producer_id == expect->producer_id &&
                  r->arg.item.sequence == expect->sequence && item_payload_ok(r->arg.item),
              "%s: get() by %s returned %s, expected %s (FIFO order)", ctx,
              r->name, item_str(r->arg.item, b1), item_str(*expect, b2));
    }
}

/* NOLINTBEGIN(cert-err34-c) — trace files are instructor-generated; a non-matching line
 * falls through to hw_die with file:line. */
static void check_sems_line(struct bounded_buffer *bb, int e, int f, const char *ctx)
{
    if (e < 0 || f < 0 || (size_t)e + (size_t)f != bb->capacity)
        hw_die("%s: generator bug — 'sems %d %d' does not sum to the capacity", ctx, e, f);
    CHECK_INVARIANT(bb_quiescent_ok(bb, (size_t)f),
                    "%s: semaphore/ring state diverged from the hand-checked table at this "
                    "quiescent checkpoint (see above)", ctx);
}

static void run_trace_file(const char *name)
{
    /* static: a failed run may abandon a thread still blocked on the buffer — it must not
     * dangle into a dead stack frame while later tests run. */
    static struct bounded_buffer bb;
    bool bb_ready = false;
    char path[256], line[256], ctx[300];
    unsigned lineno = 0;

    reset_checkers();
    item_log_init(&g_consumed);
    g_nroles = 0;

    snprintf(path, sizeof path, "trace/%s.trace", name);
    FILE *fp = open_or_die(path);
    while (next_line(fp, line, sizeof line, &lineno) && !hw_test_failed()) {
        char role[16], itok[16];
        size_t cap;
        int e, f;
        item_t item;

        snprintf(ctx, sizeof ctx, "%s:%u", path, lineno);
        if (sscanf(line, " capacity %zu", &cap) == 1) {
            if (bb_ready)
                hw_die("%s: capacity given twice", ctx);
            bb_create(&bb, cap);
            bb_ready = true;
            continue;
        }
        if (!bb_ready)
            hw_die("%s: the trace must start with 'capacity N'", ctx);

        if (sscanf(line, " begin-put %15s %15s", role, itok) == 2 &&
            parse_item(itok, &item)) {
            begin_op(&bb, role_get(role), true, item, true, ctx);
        } else if (sscanf(line, " await-put %15s", role) == 1) {
            await_op(role_get(role), true, NULL, true, ctx);
        } else if (sscanf(line, " begin-get %15s", role) == 1) {
            begin_op(&bb, role_get(role), false, item_poison(), true, ctx);
        } else if (sscanf(line, " await-get %15s %15s", role, itok) == 2 &&
                   parse_item(itok, &item)) {
            await_op(role_get(role), false, &item, true, ctx);
        } else if (sscanf(line, " put %15s %15s", role, itok) == 2 &&
                   parse_item(itok, &item)) {
            role_t *r = role_get(role);

            begin_op(&bb, r, true, item, false, ctx);
            await_op(r, true, NULL, false, ctx);
        } else if (sscanf(line, " get %15s %15s", role, itok) == 2 &&
                   parse_item(itok, &item)) {
            role_t *r = role_get(role);

            begin_op(&bb, r, false, item_poison(), false, ctx);
            await_op(r, false, &item, false, ctx);
        } else if (sscanf(line, " sems %d %d", &e, &f) == 2) {
            check_sems_line(&bb, e, f, ctx);
        } else {
            hw_die("%s: unrecognized trace line: %s", ctx, line);
        }
    }
    fclose(fp);

    /* Abandon anything still in flight (only possible after a failure). */
    for (size_t i = 0; i < g_nroles; i++) {
        if (g_roles[i].task) {
            course_task_abandon(g_roles[i].task);
            g_roles[i].task = NULL;
        }
    }

    /* Compare with the answer file. */
    snprintf(path, sizeof path, "trace/%s.answer", name);
    fp = open_or_die(path);
    lineno = 0;
    while (next_line(fp, line, sizeof line, &lineno)) {
        int e, f;

        snprintf(ctx, sizeof ctx, "%s:%u", path, lineno);
        if (strncmp(line, "consumed", 8) == 0) {
            char *tok = strtok(line + 8, " \t\n\r"); /* NOLINT(concurrency-mt-unsafe) */
            size_t i = 0;
            char b1[ITEM_STR_LEN], b2[ITEM_STR_LEN];

            while (tok) {
                item_t want;

                if (!parse_item(tok, &want))
                    hw_die("%s: bad item '%s'", ctx, tok);
                if (i < g_consumed.len) {
                    item_t got = g_consumed.items[i];

                    CHECK(got.producer_id == want.producer_id && got.sequence == want.sequence,
                          "%s: consumed[%zu] is %s, expected %s", ctx, i,
                          item_str(got, b1), item_str(want, b2));
                }
                i++;
                tok = strtok(NULL, " \t\n\r"); /* NOLINT(concurrency-mt-unsafe) */
            }
            CHECK(g_consumed.len == i,
                  "%s: consumed %zu items in total, expected %zu", ctx, g_consumed.len, i);
        } else if (strncmp(line, "final-empty", 11) == 0 && bb_ready) {
            CHECK_INVARIANT(bb_quiescent_ok(&bb, 0),
                            "%s: the buffer state after the trace differs from the expected final "
                            "state (see above)", ctx);
        } else if (sscanf(line, " sems %d %d", &e, &f) == 2 && bb_ready) {
            check_sems_line(&bb, e, f, ctx);
        } else {
            hw_die("%s: unrecognized answer line: %s", ctx, line);
        }
    }
    fclose(fp);
    expect_no_violations(name);

    if (bb_ready && !hw_test_failed())
        bb_destroy(&bb); /* keep the buffer alive if ops may still be blocked on it */
    item_log_destroy(&g_consumed);
}
/* NOLINTEND(cert-err34-c) */

static void test_trace_session(void)
{
    run_trace_file("session");
}

/* ---- producer/consumer fleets ----------------------------------------------------------------
 * Spawns nprod producers and ncons consumers (instructor workers own poison/termination),
 * joins everything, and leaves each consumer's log in cons[i].log for verification. */

#define MAX_THREADS 8

static void run_fleet(struct bounded_buffer *bb, unsigned nprod, unsigned ncons,
                      unsigned per_prod, q2_consumer_args_t *cons)
{
    pthread_t ptid[MAX_THREADS], ctid[MAX_THREADS];
    q2_producer_args_t prod[MAX_THREADS];

    for (unsigned i = 0; i < ncons; i++) {
        cons[i].bb = bb;
        item_log_init(&cons[i].log);
        if (pthread_create(&ctid[i], NULL, q2_consumer_worker, &cons[i]) != 0)
            hw_die("cannot create consumer threads");
    }
    for (unsigned i = 0; i < nprod; i++) {
        prod[i] = (q2_producer_args_t){.bb = bb, .producer_id = i, .count = per_prod};
        if (pthread_create(&ptid[i], NULL, q2_producer_worker, &prod[i]) != 0)
            hw_die("cannot create producer threads");
    }
    for (unsigned i = 0; i < nprod; i++)
        pthread_join(ptid[i], NULL);
    q2_send_poison(bb, ncons);
    for (unsigned i = 0; i < ncons; i++)
        pthread_join(ctid[i], NULL);
}

static void test_spsc_exact_fifo(void)
{
    for (size_t cap = 1; cap <= 3; cap++) {
        struct bounded_buffer bb;
        q2_consumer_args_t cons[1];
        unsigned n = 5000 * hw_stress_scale();

        reset_checkers();
        bb_create(&bb, cap);
        run_fleet(&bb, 1, 1, n, cons);
        CHECK(verify_exact_fifo(&cons[0].log, 0, n),
              "capacity %zu: get() results of a 1-producer/1-consumer run are not in put() "
              "order (see above)", cap);
        CHECK_INVARIANT(bb_quiescent_ok(&bb, 0), "capacity %zu: buffer/semaphore state "
                        "inconsistent after the run", cap);
        expect_no_violations("spsc");
        bb_destroy(&bb);
        item_log_destroy(&cons[0].log);
        if (hw_test_failed())
            return;
    }
}

static void test_mpsc_per_producer_order(void)
{
    struct bounded_buffer bb;
    q2_consumer_args_t cons[1];
    unsigned n = 3000 * hw_stress_scale();

    reset_checkers();
    bb_create(&bb, 8);
    run_fleet(&bb, 4, 1, n, cons);
    CHECK(verify_per_producer_order(&cons[0].log, 4, n),
          "a producer's items reached the single consumer out of that producer's put() order (see above)");
    CHECK_INVARIANT(bb_quiescent_ok(&bb, 0), "buffer/semaphore state inconsistent after "
                    "the run");
    expect_no_violations("mpsc");
    bb_destroy(&bb);
    item_log_destroy(&cons[0].log);
}

static void test_mpmc_exactly_once(void)
{
    enum { NPROD = 4, NCONS = 4 };
    struct bounded_buffer bb;
    q2_consumer_args_t cons[NCONS];
    item_log_t logs[NCONS];
    unsigned n = 5000 * hw_stress_scale();

    reset_checkers();
    bb_create(&bb, 8);
    run_fleet(&bb, NPROD, NCONS, n, cons);
    for (unsigned i = 0; i < NCONS; i++)
        logs[i] = cons[i].log;
    CHECK(verify_exactly_once(logs, NCONS, NPROD, n),
          "exactly-once check failed: items were lost, duplicated, fabricated or "
          "corrupted (see above)");
    CHECK_INVARIANT(bb_quiescent_ok(&bb, 0), "buffer/semaphore state inconsistent after "
                    "the run");
    expect_no_violations("mpmc");
    bb_destroy(&bb);
    for (unsigned i = 0; i < NCONS; i++)
        item_log_destroy(&cons[i].log);
}

/* ---- runner ---------------------------------------------------------------------------------- */

/* The three stress tests carry their own 60 s watchdog (0 = the quiz default of 20 s): with
 * spinning waiters they are scheduler-bound on 2 CPUs, and the reference measured up to 11 s
 * under ASan on a fast 2-core pin — a correct answer on a slow VM needs the margin. A wrong
 * answer still fails fast in the unit tests and the checkers, which run first. */
#define STRESS_TIMEOUT_S 60

static const hw_test_t TESTS[] = {
    {"spinlock_basics", test_spinlock_basics,
     "your spinlock alone: a free lock is taken, a held lock makes a waiter spin, release "
     "hands it over", 0},
    {"sem_basics", test_sem_basics,
     "your semaphore alone: waits take units, value-0 waits block until a post, the value "
     "never goes negative, one post releases exactly one of two waiters", 0},
    {"trace_session", test_trace_session,
     "replays trace/session.trace: blocking on empty/full, FIFO, sems checkpoints", 0},
    {"spsc_exact_fifo", test_spsc_exact_fifo,
     "capacities 1-3, 1P/1C: exact FIFO through thousands of full<->empty wraparounds", STRESS_TIMEOUT_S},
    {"mpsc_per_producer_order", test_mpsc_per_producer_order,
     "4 producers, 1 consumer: each producer's subsequence arrives in order", STRESS_TIMEOUT_S},
    {"mpmc_exactly_once", test_mpmc_exactly_once,
     "4P/4C stress: exactly-once set equality over per-consumer logs", STRESS_TIMEOUT_S},
};

static bool usage_only(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0 || strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-h") == 0)
            return true;
    }
    return false;
}

int main(int argc, char **argv)
{
    /* The spinning semaphore (layer 2) never yields: on ONE CPU every waiter burns its whole
     * timeslice before the thread it waits for can run, and the fleet tests take minutes
     * instead of a second — the reference solution itself trips the watchdog. Refuse to run
     * there rather than hand out a false FAIL. */
    if (!usage_only(argc, argv) && course_ncpus() < 2) {
        fprintf(stderr,
                "UNSUPPORTED: Quiz Q2 needs at least 2 CPUs this process may run on (found "
                "%ld). Its spinning semaphore never yields, so on one CPU the tests take "
                "minutes and the watchdog fires even for a correct solution. Give the "
                "VM/container a second core and rerun.\n",
                course_ncpus());
        return COURSE_EXIT_UNSUPPORTED;
    }
    return hw_run_tests("Quiz Q2 (" HW_IMPL_NAME ") — xchg -> spinlock -> semaphore -> "
                        "bounded buffer", argc, argv, TESTS,
                        sizeof TESTS / sizeof TESTS[0]);
}
