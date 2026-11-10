/* main.c — Homework B tests (instructor supplied; do not edit).
 *
 * trace_session replays trace/session.trace step by step: begin-* steps REQUIRE the call to
 * block (the runner starts it on its own thread, waits course_settle_ms, and fails if it
 * completed), await-* steps require it to finish once the trace unblocks it, and `sems E F`
 * checkpoints compare the two counting semaphores against the hand-checked table at
 * quiescent points. The concurrent tests verify FIFO order (SPSC), per-producer order
 * (MPSC), and exactly-once integrity (MPMC) over instructor-owned per-consumer logs. The
 * semaphore choke point (src/sem.c) checks the protocol underneath throughout: wait order,
 * mutex discipline per put()/get() call, initial values.
 */
#include "bounded_buffer.h"
#include "check.h"
#include "course_runtime.h"
#include "instrument.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#ifndef HW_IMPL_NAME
#define HW_IMPL_NAME "student"
#endif

#define AWAIT_MS 5000

static void expect_no_violations(const char *ctx)
{
    CHECK_INVARIANT(semstat_violations() == 0,
                    "%s: %lu semaphore-protocol violation(s) at the sem_wait/sem_post choke "
                    "point — see the INVARIANT lines above", ctx, semstat_violations());
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

    semstat_call_begin("put");
    put(op->bb, op->item);
    semstat_call_end();
    return NULL;
}

static void *get_thunk(void *arg)
{
    op_arg_t *op = arg;

    semstat_call_begin("get");
    op->item = get(op->bb);
    semstat_call_end();
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
            CHECK(false, "%s: %s by %s completed but must BLOCK here — %s", ctx,
                  is_put ? "put" : "get", r->name,
                  is_put ? "the buffer is FULL, so put must sleep in sem_wait(&empty)"
                         : "the buffer is EMPTY, so get must sleep in sem_wait(&full)");
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
            CHECK(false, "%s: %s by %s is still blocked %d ms after the trace unblocked it — "
                  "did the opposite call sem_post the %s semaphore it waits on?", ctx,
                  is_put ? "put" : "get", r->name, AWAIT_MS, is_put ? "'empty'" : "'full'");
        else
            CHECK(false, "%s: %s by %s did not complete within %d ms although it must NOT "
                  "block here — check the sem_init starting values (see the header)", ctx,
                  is_put ? "put" : "get", r->name, AWAIT_MS);
        course_task_abandon(r->task);
        r->task = NULL;
        return;
    }
    r->task = NULL;
    if (expect) {
        CHECK(r->arg.item.producer_id == expect->producer_id &&
                  r->arg.item.sequence == expect->sequence && item_payload_ok(r->arg.item),
              "%s: %s got %s, expected %s — the buffer must be FIFO through fill/use", ctx,
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

    semstat_reset();
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
                            "%s: the buffer must end empty, with all semaphores back at "
                            "their starting values (see above)", ctx);
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

/* ---- block on empty / block on full, then one wakeup ----------------------------------------- */

static void do_put(struct bounded_buffer *bb, item_t item)
{
    semstat_call_begin("put");
    put(bb, item);
    semstat_call_end();
}

static item_t do_get(struct bounded_buffer *bb)
{
    semstat_call_begin("get");
    item_t it = get(bb);

    semstat_call_end();
    return it;
}

static void test_block_on_empty_then_wake(void)
{
    static struct bounded_buffer bb; /* may keep an abandoned waiter after a failure */
    static op_arg_t op;

    op = (op_arg_t){.bb = &bb};

    semstat_reset();
    item_log_init(&g_consumed);
    bb_create(&bb, 4);
    course_task_t *t = course_task_start(get_thunk, &op);

    course_sleep_ms(course_settle_ms());
    if (course_task_done(t)) {
        CHECK(false, "get() on an EMPTY buffer returned immediately — it must sleep in "
              "sem_wait(&full) until a put fills a slot");
        course_task_await(t, AWAIT_MS);
    } else {
        do_put(&bb, item_make(0, 0));
        if (!course_task_await(t, AWAIT_MS)) {
            CHECK(false, "the blocked consumer was not woken by a put within %d ms — "
                  "did put() sem_post(&full)?", AWAIT_MS);
            course_task_abandon(t);
        } else {
            CHECK(op.item.producer_id == 0 && op.item.sequence == 0 && item_payload_ok(op.item),
                  "the woken consumer got the wrong item (p%u, #%u)", op.item.producer_id,
                  op.item.sequence);
        }
    }
    CHECK_INVARIANT(bb_quiescent_ok(&bb, 0), "buffer/semaphore state inconsistent at the end");
    expect_no_violations("block on empty");
    if (!hw_test_failed())
        bb_destroy(&bb);
    item_log_destroy(&g_consumed);
}

static void test_block_on_full_then_wake(void)
{
    static struct bounded_buffer bb; /* may keep an abandoned waiter after a failure */
    static op_arg_t op;

    op = (op_arg_t){.bb = &bb, .item = item_make(0, 2)};

    semstat_reset();
    item_log_init(&g_consumed);
    bb_create(&bb, 2);
    do_put(&bb, item_make(0, 0));
    do_put(&bb, item_make(0, 1));

    course_task_t *t = course_task_start(put_thunk, &op);

    course_sleep_ms(course_settle_ms());
    if (course_task_done(t)) {
        CHECK(false, "put() on a FULL buffer returned immediately — it must sleep in "
              "sem_wait(&empty) until a get frees a slot");
        course_task_await(t, AWAIT_MS); /* done: join and free the record */
        t = NULL;
    } else {
        item_t first = do_get(&bb);

        CHECK(first.sequence == 0, "FIFO broken: first get returned #%u, expected #0",
              first.sequence);
        if (!course_task_await(t, AWAIT_MS)) {
            CHECK(false, "the blocked producer was not woken by a get within %d ms — "
                  "did get() sem_post(&empty)?", AWAIT_MS);
            course_task_abandon(t);
            t = NULL;
        }
    }
    if (t && !hw_test_failed()) {
        for (unsigned want = 1; want <= 2; want++) {
            item_t it = do_get(&bb);

            CHECK(it.sequence == want, "FIFO broken after the wakeup: got #%u, expected #%u",
                  it.sequence, want);
        }
        CHECK_INVARIANT(bb_quiescent_ok(&bb, 0), "buffer/semaphore state inconsistent at "
                        "the end");
        bb_destroy(&bb);
    }
    expect_no_violations("block on full");
    item_log_destroy(&g_consumed);
}

/* ---- producer/consumer fleets ----------------------------------------------------------------
 * Spawns nprod producers and ncons consumers (instructor workers own poison/termination),
 * joins everything, and leaves each consumer's log in cons[i].log for verification. */

#define MAX_THREADS 8

static void run_fleet(struct bounded_buffer *bb, unsigned nprod, unsigned ncons,
                      unsigned per_prod, bb_consumer_args_t *cons)
{
    pthread_t ptid[MAX_THREADS], ctid[MAX_THREADS];
    bb_producer_args_t prod[MAX_THREADS];

    for (unsigned i = 0; i < ncons; i++) {
        cons[i].bb = bb;
        item_log_init(&cons[i].log);
        if (pthread_create(&ctid[i], NULL, bb_consumer_worker, &cons[i]) != 0)
            hw_die("cannot create consumer threads");
    }
    for (unsigned i = 0; i < nprod; i++) {
        prod[i] = (bb_producer_args_t){.bb = bb, .producer_id = i, .count = per_prod};
        if (pthread_create(&ptid[i], NULL, bb_producer_worker, &prod[i]) != 0)
            hw_die("cannot create producer threads");
    }
    for (unsigned i = 0; i < nprod; i++)
        pthread_join(ptid[i], NULL);
    bb_send_poison(bb, ncons);
    for (unsigned i = 0; i < ncons; i++)
        pthread_join(ctid[i], NULL);
}

static void test_wraparound_small_caps(void)
{
    for (size_t cap = 2; cap <= 4; cap++) {
        struct bounded_buffer bb;
        bb_consumer_args_t cons[1];
        unsigned n = 5000 * hw_stress_scale();

        semstat_reset();
        bb_create(&bb, cap);
        run_fleet(&bb, 1, 1, n, cons);
        CHECK(verify_exact_fifo(&cons[0].log, 0, n),
              "capacity %zu: a 1-producer/1-consumer run must come out in exact FIFO order "
              "(wraparound at fill/use?)", cap);
        CHECK_INVARIANT(bb_quiescent_ok(&bb, 0), "capacity %zu: buffer/semaphore state "
                        "inconsistent after the run", cap);
        expect_no_violations("wraparound");
        bb_destroy(&bb);
        item_log_destroy(&cons[0].log);
        if (hw_test_failed())
            return;
    }
}

static void test_mpsc_per_producer_order(void)
{
    struct bounded_buffer bb;
    bb_consumer_args_t cons[1];
    unsigned n = 5000 * hw_stress_scale();

    semstat_reset();
    bb_create(&bb, 8);
    run_fleet(&bb, 4, 1, n, cons);
    CHECK(verify_per_producer_order(&cons[0].log, 4, n),
          "each producer's items must reach the single consumer in that producer's order");
    CHECK_INVARIANT(bb_quiescent_ok(&bb, 0), "buffer/semaphore state inconsistent after "
                    "the run");
    expect_no_violations("mpsc");
    bb_destroy(&bb);
    item_log_destroy(&cons[0].log);
}

static void run_mpmc(size_t cap, unsigned per_prod)
{
    enum { NPROD = 4, NCONS = 4 };
    struct bounded_buffer bb;
    bb_consumer_args_t cons[NCONS];

    semstat_reset();
    bb_create(&bb, cap);
    run_fleet(&bb, NPROD, NCONS, per_prod, cons);

    item_log_t logs[NCONS];

    for (unsigned i = 0; i < NCONS; i++)
        logs[i] = cons[i].log;
    CHECK(verify_exactly_once(logs, NCONS, NPROD, per_prod),
          "capacity %zu: exactly-once integrity failed — items were lost, duplicated, "
          "fabricated or corrupted (see above)", cap);
    CHECK_INVARIANT(bb_quiescent_ok(&bb, 0), "capacity %zu: buffer/semaphore state "
                    "inconsistent after the run", cap);
    expect_no_violations("mpmc");
    bb_destroy(&bb);
    for (unsigned i = 0; i < NCONS; i++)
        item_log_destroy(&cons[i].log);
}

static void test_mpmc_exactly_once(void)
{
    run_mpmc(8, 20000 * hw_stress_scale());
}

static void test_mpmc_small_cap(void)
{
    run_mpmc(2, 5000 * hw_stress_scale());
}

/* ---- runner ---------------------------------------------------------------------------------- */

static const hw_test_t TESTS[] = {
    {"trace_session", test_trace_session,
     "replays trace/session.trace: blocking on empty/full, FIFO, sems checkpoints", 0},
    {"block_on_empty_then_wake", test_block_on_empty_then_wake,
     "get on an empty buffer blocks; one put wakes it with the right item", 0},
    {"block_on_full_then_wake", test_block_on_full_then_wake,
     "put on a full buffer blocks; one get wakes it; FIFO preserved", 0},
    {"wraparound_small_caps", test_wraparound_small_caps,
     "capacities 2-4, 1P/1C: exact FIFO through thousands of full<->empty wraparounds", 0},
    {"mpsc_per_producer_order", test_mpsc_per_producer_order,
     "4 producers, 1 consumer: each producer's subsequence arrives in order", 0},
    {"mpmc_exactly_once", test_mpmc_exactly_once,
     "4P/4C stress, capacity 8: exactly-once set equality over per-consumer logs", 0},
    {"mpmc_small_cap", test_mpmc_small_cap,
     "4P/4C stress at capacity 2: hammers the full<->empty transitions on both semaphores",
     0},
};

int main(int argc, char **argv)
{
    return hw_run_tests("Homework B (" HW_IMPL_NAME ") — bounded buffer: semaphores", argc,
                        argv, TESTS, sizeof TESTS / sizeof TESTS[0]);
}
