/* main.c — Homework C tests (instructor supplied; do not edit).
 *
 * The lock()/unlock() wrappers (src/lock_check.c) are the choke point: every acquisition and
 * release in transfer() flows through them, so a wrong lock order, a double lock, a leaked
 * lock, or a transfer that never locked both accounts (single-lock or private/global-lock
 * "solutions") is reported DETERMINISTICALLY on its first occurrence — no unlucky schedule
 * needed. The concurrent tests additionally act as live deadlock probes: if the protocol is
 * wrong, the watchdog turns the hang into exit 124.
 *
 * trace_* tests load trace/<name>.trace, replay it step by step against transfer(), and check
 * the final state against trace/<name>.answer. Run from the module directory (run-all-tests.sh does).
 */
#include "bank.h"
#include "check.h"
#include "course_runtime.h"
#include "lock_check.h"

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifndef HW_IMPL_NAME
#define HW_IMPL_NAME "student"
#endif

/* ---- the bank under test -------------------------------------------------------------------- */

#define MAX_ACCOUNTS 16

static account_t g_accounts[MAX_ACCOUNTS];
static size_t g_naccounts;

static void bank_add_account(unsigned id, long balance)
{
    if (g_naccounts == MAX_ACCOUNTS)
        hw_die("too many accounts (max %d)", MAX_ACCOUNTS);
    if (g_naccounts > 0 && g_accounts[g_naccounts - 1].id >= id)
        hw_die("account ids must be added in ascending order (%u after %u)", id,
               g_accounts[g_naccounts - 1].id);
    account_init(&g_accounts[g_naccounts], id, balance); /* registers with the checker too */
    g_naccounts++;
}

/* ids[] must be ascending; account_init registers every lock with the order checker. */
static void bank_setup(const unsigned *ids, const long *balances, size_t n)
{
    lock_check_reset();
    g_naccounts = 0;
    for (size_t i = 0; i < n; i++)
        bank_add_account(ids[i], balances[i]);
}

static void bank_teardown(void)
{
    for (size_t i = 0; i < g_naccounts; i++)
        account_destroy(&g_accounts[i]);
    g_naccounts = 0;
}

static account_t *bank_find(unsigned id)
{
    for (size_t i = 0; i < g_naccounts; i++) {
        if (g_accounts[i].id == id)
            return &g_accounts[i];
    }
    return NULL;
}

/* After every transfer call on this thread: no lock may still be held, no violation recorded. */
static void expect_clean(const char *ctx)
{
    CHECK_INVARIANT(lock_check_held_by_me() == 0,
                    "%s: transfer() returned while still holding %zu account lock(s) — release "
                    "every acquired lock on every path", ctx, lock_check_held_by_me());
    CHECK_INVARIANT(lock_check_violations() == 0,
                    "%s: %lu locking-protocol violation(s) — see the INVARIANT lines above",
                    ctx, lock_check_violations());
}

/* Every transfer() call in this file goes through here. The choke-point counters must show
 * both account locks acquired (one for a self-transfer) and nothing still held: 0 or 1
 * acquisitions means the balances were read behind a private/global lock (or none), which
 * no other thread's protocol respects. First failure only — later calls stay quiet. */
static bool checked_transfer(account_t *from, account_t *to, long amount)
{
    unsigned long before = lock_check_my_acquisitions();
    bool result = transfer(from, to, amount);
    unsigned long acquired = lock_check_my_acquisitions() - before;
    unsigned long want = from == to ? 1UL : 2UL;

    if (!hw_test_failed()) {
        CHECK_INVARIANT(lock_check_held_by_me() == 0,
                        "transfer() returned while still holding %zu account lock(s) — "
                        "release every acquired lock on every path", lock_check_held_by_me());
        CHECK_INVARIANT(acquired >= want,
                        "transfer() completed after acquiring %lu account lock(s), expected "
                        "%lu — funds and balances must be protected by the ACCOUNTS' own "
                        "locks; a private or global lock is forbidden (and protects nothing "
                        "the harness and other threads read)", acquired, want);
    }
    return result;
}

/* ---- trace runner: replay trace/<name>.trace, compare with trace/<name>.answer -------------- */

/* Strip comments/blank lines; returns false at EOF. Keeps a 1-based line number in *lineno. */
static bool next_line(FILE *fp, char *buf, size_t bufsz, unsigned *lineno)
{
    while (fgets(buf, (int)bufsz, fp)) {
        (*lineno)++;
        char *hash = strchr(buf, '#');

        if (hash)
            *hash = '\0';
        /* skip lines that are empty after comment stripping */
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

/* NOLINTBEGIN(cert-err34-c) — trace files are instructor-authored; a parse that matches none
 * of the sscanf patterns falls through to hw_die with the file and line number. */
static void run_trace_file(const char *name)
{
    char path[256], line[256];
    unsigned lineno = 0;

    lock_check_reset();
    g_naccounts = 0;

    /* Phase 1: build the bank and replay the steps. */
    snprintf(path, sizeof path, "trace/%s.trace", name);
    FILE *fp = open_or_die(path);
    while (next_line(fp, line, sizeof line, &lineno)) {
        unsigned id, from_id, to_id;
        long balance, amount;
        char verdict[8];

        if (sscanf(line, " account %u %ld", &id, &balance) == 2) {
            bank_add_account(id, balance);
        } else if (sscanf(line, " transfer %u %u %ld %7s", &from_id, &to_id, &amount,
                          verdict) == 4) {
            account_t *from = bank_find(from_id), *to = bank_find(to_id);
            bool want = strcmp(verdict, "ok") == 0;

            if (!from || !to)
                hw_die("%s:%u: unknown account id", path, lineno);
            bool got = checked_transfer(from, to, amount);

            CHECK(got == want,
                  "%s:%u: transfer %u -> %u of %ld returned %s, expected %s", path, lineno,
                  from_id, to_id, amount, got ? "ok" : "fail", want ? "ok" : "fail");
            char ctx[sizeof path + 16];
            snprintf(ctx, sizeof ctx, "%s:%u", path, lineno);
            expect_clean(ctx);
        } else {
            hw_die("%s:%u: unrecognized trace line: %s", path, lineno, line);
        }
    }
    fclose(fp);

    /* Phase 2: compare the final state with the answer file. */
    snprintf(path, sizeof path, "trace/%s.answer", name);
    fp = open_or_die(path);
    lineno = 0;
    while (next_line(fp, line, sizeof line, &lineno)) {
        unsigned id;
        long want;

        if (sscanf(line, " balance %u %ld", &id, &want) == 2) {
            account_t *a = bank_find(id);

            if (!a)
                hw_die("%s:%u: unknown account id %u", path, lineno, id);
            CHECK(a->balance == want,
                  "final balance of account %u is %ld, expected %ld (%s:%u)", id, a->balance,
                  want, path, lineno);
        } else if (sscanf(line, " total %ld", &want) == 1) {
            long got = bank_total(g_accounts, g_naccounts);

            CHECK(got == want,
                  "total money is %ld, expected %ld — transfers must conserve money", got,
                  want);
        } else {
            hw_die("%s:%u: unrecognized answer line: %s", path, lineno, line);
        }
    }
    fclose(fp);
    bank_teardown();
}
/* NOLINTEND(cert-err34-c) */

static void test_trace_basic(void)
{
    run_trace_file("basic");
}

static void test_trace_edges(void)
{
    run_trace_file("edges");
}

/* ---- self_transfer: from == to must not double-lock the nonrecursive mutex ----------------- */

static void test_self_transfer(void)
{
    const unsigned ids[] = {7};
    const long balances[] = {100};

    bank_setup(ids, balances, 1);
    CHECK(checked_transfer(&g_accounts[0], &g_accounts[0], 60) == true,
          "self-transfer with sufficient funds must succeed");
    CHECK(g_accounts[0].balance == 100, "a self-transfer must leave the balance unchanged");
    expect_clean("self-transfer (sufficient)");

    CHECK(checked_transfer(&g_accounts[0], &g_accounts[0], 101) == false,
          "self-transfer of more than the balance must fail");
    CHECK(g_accounts[0].balance == 100, "a failed self-transfer must change nothing");
    expect_clean("self-transfer (insufficient)");
    bank_teardown();
}

/* ---- insufficient_funds: the failure path must change nothing and release everything ------- */

static void test_insufficient_funds(void)
{
    const unsigned ids[] = {1, 2};
    const long balances[] = {30, 40};

    bank_setup(ids, balances, 2);
    CHECK(checked_transfer(&g_accounts[0], &g_accounts[1], 31) == false,
          "transferring 31 out of a balance of 30 must fail");
    CHECK(g_accounts[0].balance == 30 && g_accounts[1].balance == 40,
          "a failed transfer must apply NEITHER balance change (got %ld and %ld)",
          g_accounts[0].balance, g_accounts[1].balance);
    expect_clean("insufficient funds");

    /* The locks must still be usable in both directions after the failure path. */
    CHECK(checked_transfer(&g_accounts[1], &g_accounts[0], 5) == true,
          "transfer after a failed one must work — did the failure path leak a lock?");
    CHECK(checked_transfer(&g_accounts[0], &g_accounts[1], 35) == true,
          "transfer in the original direction must work after the failure path");
    CHECK(g_accounts[0].balance == 0 && g_accounts[1].balance == 70,
          "balances after the recovery transfers are %ld and %ld, expected 0 and 70",
          g_accounts[0].balance, g_accounts[1].balance);
    expect_clean("recovery after failure");
    bank_teardown();
}

/* ---- order_independent_of_layout: any ONE fixed order is fine, whatever its key -----------
 * Every other bank in this file is an array sorted by id. Here the ids are laid out 20, 10,
 * 30 at ascending addresses, so "lower id first", "lower address first" and "lower array
 * index first" are three DIFFERENT orders — each of them consistent, so each must pass. The
 * checker records held-before edges and objects only to a contradiction between two
 * acquisitions; the key a solution orders by is its own business. Single-threaded. */

static void test_order_independent_of_layout(void)
{
    static account_t acc[3]; /* ids 20, 10, 30 at ascending addresses */
    const unsigned ids[3] = {20, 10, 30};

    lock_check_reset();
    g_naccounts = 0;
    for (size_t i = 0; i < 3; i++)
        account_init(&acc[i], ids[i], 500);
    for (size_t a = 0; a < 3; a++) {
        for (size_t b = 0; b < 3; b++) {
            if (a == b)
                continue;
            CHECK(checked_transfer(&acc[a], &acc[b], 10) == true, "%u -> %u must succeed",
                  ids[a], ids[b]);
            char ctx[64];

            snprintf(ctx, sizeof ctx, "%u -> %u (ids not in address order)", ids[a], ids[b]);
            expect_clean(ctx);
        }
    }
    CHECK(acc[0].balance == 500 && acc[1].balance == 500 && acc[2].balance == 500,
          "balances are %ld, %ld, %ld, expected 500 each", acc[0].balance, acc[1].balance,
          acc[2].balance);
    for (size_t i = 0; i < 3; i++)
        account_destroy(&acc[i]);
}

/* ---- abba_stress: opposite-direction transfers on two accounts --------------------------------
 * With a wrong lock order this is the classic ABBA hang (watchdog -> exit 124), and the order
 * checker flags the very first backwards acquisition even if the schedule never deadlocks. */

typedef struct {
    account_t *from, *to;
    unsigned iters;
} pair_worker_args_t;

static void *pair_worker(void *arg)
{
    pair_worker_args_t *w = arg;

    for (unsigned i = 0; i < w->iters; i++)
        checked_transfer(w->from, w->to, 1);
    return NULL;
}

static void test_abba_stress(void)
{
    enum { THREADS = 4 };
    const unsigned ids[] = {1, 2};
    const long balances[] = {1000000, 1000000};
    pthread_t tids[THREADS];
    pair_worker_args_t args[THREADS];
    unsigned iters = 20000 * hw_stress_scale();

    bank_setup(ids, balances, 2);
    for (unsigned t = 0; t < THREADS; t++) {
        /* Even threads move 1 -> 2, odd threads move 2 -> 1: forces lock nesting both ways. */
        args[t].from = &g_accounts[t % 2];
        args[t].to = &g_accounts[1 - t % 2];
        args[t].iters = iters;
        if (pthread_create(&tids[t], NULL, pair_worker, &args[t]) != 0)
            hw_die("cannot create worker threads");
    }
    for (unsigned t = 0; t < THREADS; t++)
        pthread_join(tids[t], NULL);

    CHECK(bank_total(g_accounts, 2) == 2000000,
          "total money changed under contention: %ld, expected 2000000 — the funds check or "
          "the updates ran outside the locks", bank_total(g_accounts, 2));
    CHECK_INVARIANT(lock_check_violations() == 0,
                    "%lu locking-protocol violation(s) under contention — see above",
                    lock_check_violations());
    bank_teardown();
}

/* ---- overdraft_race: many threads race for the LAST unit of money --------------------------
 * Account 1 holds exactly 1; every thread tries to move 1 to account 2 at the same moment.
 * Correct code checks funds while holding both locks, so exactly ONE transfer can succeed.
 * Code that checks funds before locking reads a stale balance and overdraws the account.
 * A 1 ms acquire delay injected inside lock() stretches the decide-to-lock -> holds-the-lock window so
 * every thread finishes its (possibly misplaced) check before any thread acquires: the broken
 * interleaving is forced on EVERY run instead of depending on scheduler luck. */

typedef struct {
    course_gate_t *arrived; /* workers count themselves in here ...            */
    course_gate_t *go;      /* ... and main fires this once ALL have arrived   */
    bool succeeded;
} overdraft_worker_args_t;

static void *overdraft_worker(void *arg)
{
    overdraft_worker_args_t *w = arg;

    course_gate_add(w->arrived, 1);  /* I am at the starting line */
    course_gate_wait_min(w->go, 1);  /* released into transfer() together with everyone */
    w->succeeded = checked_transfer(&g_accounts[0], &g_accounts[1], 1);
    return NULL;
}

static void test_overdraft_race(void)
{
    enum { THREADS = 8 };
    const unsigned ids[] = {1, 2};
    const long balances[] = {1, 0};
    unsigned rounds = 5 * hw_stress_scale();

    for (unsigned r = 0; r < rounds && !hw_test_failed(); r++) {
        pthread_t tids[THREADS];
        overdraft_worker_args_t args[THREADS];
        course_gate_t arrived, go;
        unsigned successes = 0;

        bank_setup(ids, balances, 2);
        lock_check_set_acquire_delay_us(1000);
        course_gate_init(&arrived);
        course_gate_init(&go);
        for (unsigned t = 0; t < THREADS; t++) {
            args[t] = (overdraft_worker_args_t){.arrived = &arrived, .go = &go};
            if (pthread_create(&tids[t], NULL, overdraft_worker, &args[t]) != 0)
                hw_die("cannot create worker threads");
        }
        /* Fire only once every worker is parked at the gate: thread creation and first
         * scheduling are then out of the picture, and the release skew is just the
         * condvar wakeup — the same instant for all of them, on every machine. */
        course_gate_wait_min(&arrived, THREADS);
        course_gate_set(&go, 1);
        for (unsigned t = 0; t < THREADS; t++) {
            pthread_join(tids[t], NULL);
            successes += args[t].succeeded;
        }

        CHECK(successes == 1,
              "round %u: %u of %d racing transfers of the last unit succeeded — funds must be "
              "checked while BOTH locks are held, or the check races the withdrawal",
              r, successes, THREADS);
        CHECK_INVARIANT(g_accounts[0].balance >= 0,
                        "round %u: account 1 was overdrawn to %ld", r,
                        g_accounts[0].balance);
        CHECK(g_accounts[0].balance == 0 && g_accounts[1].balance == 1,
              "round %u: final balances are %ld and %ld, expected 0 and 1", r,
              g_accounts[0].balance, g_accounts[1].balance);
        CHECK_INVARIANT(lock_check_violations() == 0,
                        "round %u: %lu locking-protocol violation(s) — see above", r,
                        lock_check_violations());
        course_gate_destroy(&go);
        course_gate_destroy(&arrived);
        bank_teardown();
    }
}

/* ---- many_accounts_stress: random pairs (and self-transfers) over 16 accounts -------------- */

typedef struct {
    unsigned iters;
    uint64_t seed;
} random_worker_args_t;

static void *random_worker(void *arg)
{
    random_worker_args_t *w = arg;
    course_rng_t rng;

    course_rng_seed(&rng, w->seed);
    for (unsigned i = 0; i < w->iters; i++) {
        size_t from = course_rng_below(&rng, (uint32_t)g_naccounts);
        /* mostly distinct pairs, occasionally a deliberate self-transfer */
        size_t to = (course_rng_below(&rng, 8) == 0)
                        ? from
                        : course_rng_below(&rng, (uint32_t)g_naccounts);
        long amount = (long)course_rng_below(&rng, 50);

        checked_transfer(&g_accounts[from], &g_accounts[to], amount);
    }
    return NULL;
}

static void test_many_accounts_stress(void)
{
    enum { THREADS = 8, ACCOUNTS = 16 };
    unsigned ids[ACCOUNTS];
    long balances[ACCOUNTS];
    pthread_t tids[THREADS];
    random_worker_args_t args[THREADS];
    unsigned iters = 20000 * hw_stress_scale();

    for (size_t i = 0; i < ACCOUNTS; i++) {
        ids[i] = (unsigned)(100 + 10 * i); /* ascending but not contiguous: order is by ID */
        balances[i] = 1000;
    }
    bank_setup(ids, balances, ACCOUNTS);
    for (unsigned t = 0; t < THREADS; t++) {
        args[t].iters = iters;
        args[t].seed = 0xA5EED0000ull + t;
        if (pthread_create(&tids[t], NULL, random_worker, &args[t]) != 0)
            hw_die("cannot create worker threads");
    }
    for (unsigned t = 0; t < THREADS; t++)
        pthread_join(tids[t], NULL);

    CHECK(bank_total(g_accounts, ACCOUNTS) == 1000L * ACCOUNTS,
          "total money changed: %ld, expected %ld", bank_total(g_accounts, ACCOUNTS),
          1000L * ACCOUNTS);
    for (size_t i = 0; i < ACCOUNTS; i++)
        CHECK_INVARIANT(g_accounts[i].balance >= 0,
                        "account %u went negative (%ld): funds were checked before both locks "
                        "were held, so the check raced the withdrawal",
                        g_accounts[i].id, g_accounts[i].balance);
    CHECK_INVARIANT(lock_check_violations() == 0,
                    "%lu locking-protocol violation(s) — see above", lock_check_violations());
    bank_teardown();
}

/* ---- runner --------------------------------------------------------------------------------- */

static const hw_test_t TESTS[] = {
    {"trace_basic", test_trace_basic,
     "replays trace/basic.trace single-threaded; exact balances from trace/basic.answer", 0},
    {"trace_edges", test_trace_edges,
     "replays trace/edges.trace: self-transfer and insufficient-funds edge cases", 0},
    {"self_transfer", test_self_transfer,
     "from == to: succeeds/fails on funds, changes nothing, never double-locks", 0},
    {"insufficient_funds", test_insufficient_funds,
     "failure path changes neither balance and releases every lock", 0},
    {"order_independent_of_layout", test_order_independent_of_layout,
     "ids 20/10/30 at ascending addresses: any ONE consistent order passes, whatever its key", 0},
    {"abba_stress", test_abba_stress,
     "opposite-direction transfers on two accounts: ABBA deadlock probe + order checker", 0},
    {"overdraft_race", test_overdraft_race,
     "threads race for the last unit: catches funds checked before both locks are held", 0},
    {"many_accounts_stress", test_many_accounts_stress,
     "8 threads, random pairs over 16 accounts: money conserved, no negative balances", 0},
};

int main(int argc, char **argv)
{
    return hw_run_tests("Homework C (" HW_IMPL_NAME ") — two-account transfer, total lock order",
                        argc, argv, TESTS, sizeof TESTS / sizeof TESTS[0]);
}
