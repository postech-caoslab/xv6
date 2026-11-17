/* main.c — Quiz Q1 visible tests (instructor supplied; do not edit).
 *
 * Every lock()/unlock() in apply_transaction() flows through the checker in
 * src/lock_check.c, which reports locking-protocol violations on their first occurrence. The
 * concurrent tests also run under the watchdog (exit 124).
 */
#include "check.h"
#include "course_runtime.h"
#include "lock_check.h"
#include "q1.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#ifndef HW_IMPL_NAME
#define HW_IMPL_NAME "student"
#endif

/* ---- the bank under test --------------------------------------------------------------------- */

#define MAX_ACCOUNTS 16

static account_t g_accounts[MAX_ACCOUNTS];
static size_t g_naccounts;

static void bank_add_account(unsigned id, long balance)
{
    if (g_naccounts == MAX_ACCOUNTS)
        hw_die("too many accounts (max %d)", MAX_ACCOUNTS);
    if (g_naccounts > 0 && g_accounts[g_naccounts - 1].id >= id)
        hw_die("account ids must be added in ascending order");
    account_init(&g_accounts[g_naccounts], id, balance); /* registers with the checker too */
    g_naccounts++;
}

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

static void expect_clean(const char *ctx)
{
    CHECK_INVARIANT(lock_check_held_by_me() == 0,
                    "%s: apply_transaction() returned while this thread still holds %zu account "
                    "lock(s)", ctx, lock_check_held_by_me());
    CHECK_INVARIANT(lock_check_violations() == 0,
                    "%s: %lu locking-protocol violation(s) — see the INVARIANT lines above",
                    ctx, lock_check_violations());
}

/* Every apply_transaction() call in the tests goes through here and is compared with the
 * lock checker's counters. First failure only — later calls stay quiet. */
static bool checked_apply(transaction_entry_t entries[], size_t count)
{
    unsigned long before = lock_check_my_acquisitions();

    lock_check_reset_my_peak();
    bool result = apply_transaction(entries, count);
    unsigned long acquired = lock_check_my_acquisitions() - before;
    size_t peak = lock_check_my_peak();

    if (!hw_test_failed()) {
        CHECK_INVARIANT(lock_check_held_by_me() == 0,
                        "apply_transaction() returned while this thread still holds %zu account "
                        "lock(s)", lock_check_held_by_me());
        CHECK_INVARIANT(acquired >= count,
                        "apply_transaction() acquired only %lu account lock(s) for a %zu-entry "
                        "transaction: some involved accounts' balances were read or written "
                        "without their own lock, so other threads could change them meanwhile",
                        acquired, count);
        if (acquired >= count)
            CHECK_INVARIANT(peak >= count,
                            "apply_transaction() held at most %zu of the transaction's %zu account "
                            "locks at the same time: while it checked or updated some balances, "
                            "other threads could still change the remaining accounts, so the "
                            "transaction was not one indivisible operation", peak, count);
    }
    return result;
}

/* ---- trace runner ------------------------------------------------------------------------------ */

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

/* NOLINTBEGIN(cert-err34-c,concurrency-mt-unsafe) — instructor-authored trace files parsed
 * single-threaded; a non-matching line falls through to hw_die with file:line. */
static void test_trace_session(void)
{
    char path[256], line[256], ctx[300];
    unsigned lineno = 0;

    lock_check_reset();
    g_naccounts = 0;

    snprintf(path, sizeof path, "trace/session.trace");
    FILE *fp = open_or_die(path);
    while (next_line(fp, line, sizeof line, &lineno)) {
        unsigned id;
        long value;

        snprintf(ctx, sizeof ctx, "%s:%u", path, lineno);
        if (sscanf(line, " account %u %ld", &id, &value) == 2) {
            bank_add_account(id, value);
        } else if (strncmp(line, "txn", 3) == 0) {
            transaction_entry_t entries[Q1_MAX_ENTRIES];
            size_t count = 0;
            int want = -1;
            char *tok = strtok(line + 3, " \t\n\r");

            while (tok) {
                if (strcmp(tok, "ok") == 0) {
                    want = 1;
                } else if (strcmp(tok, "fail") == 0) {
                    want = 0;
                } else if (sscanf(tok, "%u:%ld", &id, &value) == 2) {
                    account_t *a = bank_find(id);

                    if (!a)
                        hw_die("%s: unknown account id %u", ctx, id);
                    if (count == Q1_MAX_ENTRIES)
                        hw_die("%s: too many entries", ctx);
                    entries[count++] = (transaction_entry_t){.account = a, .delta = value};
                } else {
                    hw_die("%s: bad token '%s'", ctx, tok);
                }
                tok = strtok(NULL, " \t\n\r");
            }
            if (want < 0 || count == 0)
                hw_die("%s: a txn needs entries and a trailing ok/fail", ctx);

            bool got = checked_apply(entries, count);

            CHECK(got == (want == 1), "%s: apply_transaction() returned %s, expected %s", ctx,
                  got ? "ok" : "fail", want ? "ok" : "fail");
            expect_clean(ctx);
        } else {
            hw_die("%s: unrecognized trace line: %s", ctx, line);
        }
    }
    fclose(fp);

    snprintf(path, sizeof path, "trace/session.answer");
    fp = open_or_die(path);
    lineno = 0;
    while (next_line(fp, line, sizeof line, &lineno)) {
        unsigned id;
        long want;

        snprintf(ctx, sizeof ctx, "%s:%u", path, lineno);
        if (sscanf(line, " balance %u %ld", &id, &want) == 2) {
            account_t *a = bank_find(id);

            if (!a)
                hw_die("%s: unknown account id %u", ctx, id);
            CHECK(a->balance == want, "final balance of account %u is %ld, expected %ld (%s)",
                  id, a->balance, want, ctx);
        } else if (sscanf(line, " total %ld", &want) == 1) {
            CHECK(bank_total(g_accounts, g_naccounts) == want,
                  "total money is %ld, expected %ld", bank_total(g_accounts, g_naccounts),
                  want);
        } else {
            hw_die("%s: unrecognized answer line: %s", ctx, line);
        }
    }
    fclose(fp);
    bank_teardown();
}
/* NOLINTEND(cert-err34-c,concurrency-mt-unsafe) */

/* ---- two_account_regression: the smallest transaction, entries in descending order ---------- */

static void test_two_account_regression(void)
{
    const unsigned ids[] = {5, 9};
    const long balances[] = {100, 50};

    bank_setup(ids, balances, 2);

    /* entries deliberately in DESCENDING id order */
    transaction_entry_t move[] = {{&g_accounts[1], -30}, {&g_accounts[0], +30}};

    CHECK(checked_apply(move, 2) == true, "apply_transaction() returned false for a funded 2-account transaction");
    CHECK(g_accounts[0].balance == 130 && g_accounts[1].balance == 20,
          "balances are %ld and %ld, expected 130 and 20", g_accounts[0].balance,
          g_accounts[1].balance);
    expect_clean("two-account move");

    transaction_entry_t broke[] = {{&g_accounts[0], -200}, {&g_accounts[1], +200}};

    CHECK(checked_apply(broke, 2) == false, "apply_transaction() returned true although account 5 holds 130 and its delta is -200");
    CHECK(g_accounts[0].balance == 130 && g_accounts[1].balance == 20,
          "a failed transaction changed balances: %ld and %ld, expected 130 and 20 (unchanged)",
          g_accounts[0].balance, g_accounts[1].balance);
    expect_clean("two-account failure");

    /* The first account in id order is funded; the second is short. */
    transaction_entry_t partial[] = {{&g_accounts[0], +30}, {&g_accounts[1], -30}};

    CHECK(checked_apply(partial, 2) == false, "apply_transaction() returned true although account 9 holds 20 and its delta is -30");
    CHECK(g_accounts[0].balance == 130 && g_accounts[1].balance == 20,
          "PARTIAL application: the transaction failed (account 9 is short), but balances "
          "are %ld and %ld, expected 130 and 20 (unchanged)", g_accounts[0].balance, g_accounts[1].balance);
    expect_clean("all-or-nothing");
    bank_teardown();
}

/* ---- n_account_arbitrary_order: one transaction, entries reversed --------------------------- */

static void test_n_account_arbitrary_order(void)
{
    enum { N = 8 };
    unsigned ids[N];
    long balances[N];
    transaction_entry_t entries[N];

    for (size_t i = 0; i < N; i++) {
        ids[i] = (unsigned)(10 * (i + 1));
        balances[i] = 100;
    }
    bank_setup(ids, balances, N);
    /* entries in DESCENDING id order; alternate -x/+x so the sum is zero */
    for (size_t i = 0; i < N; i++) {
        long delta = (i % 2 == 0) ? -(long)(10 + i) : (long)(9 + i);

        entries[i] = (transaction_entry_t){.account = &g_accounts[N - 1 - i], .delta = delta};
    }
    /* fix up the last delta so the sum is exactly zero */
    long sum = 0;
    for (size_t i = 0; i + 1 < N; i++)
        sum += entries[i].delta;
    entries[N - 1].delta = -sum;

    CHECK(checked_apply(entries, N) == true, "apply_transaction() returned false for a funded 8-account transaction");
    CHECK(bank_total(g_accounts, N) == 800, "total is %ld, expected 800",
          bank_total(g_accounts, N));
    for (size_t i = 0; i < N; i++) {
        long want = 100 + entries[i].delta;

        CHECK(entries[i].account->balance == want,
              "account %u holds %ld, expected %ld — its delta was not applied exactly once",
              entries[i].account->id, entries[i].account->balance, want);
    }
    expect_clean("8-account transaction in reversed entry order");
    bank_teardown();
}

/* ---- max_entries: 1 entry and Q1_MAX_ENTRIES (16) entries ----------------------------------
 * Entries come in a scrambled order. In the failing transaction the short account sits in the
 * middle of the id order and last in entry order. */

static void test_max_entries(void)
{
    enum { N = Q1_MAX_ENTRIES, SHORT = N / 2 };
    unsigned ids[N];
    long balances[N], before[N];
    transaction_entry_t entries[N];

    for (size_t i = 0; i < N; i++) {
        ids[i] = (unsigned)i * 0x11111111u; /* 0 .. 0xFFFFFFFF: the full id range */
        balances[i] = 100;
    }
    bank_setup(ids, balances, N);

    /* 1 entry: zero-sum forces delta 0 — still one account, locked and released */
    transaction_entry_t one[] = {{&g_accounts[SHORT], 0}};

    CHECK(checked_apply(one, 1) == true, "apply_transaction() returned false for a 1-entry transaction (delta 0)");
    CHECK(g_accounts[SHORT].balance == 100, "a delta-0 entry changed the balance to %ld",
          g_accounts[SHORT].balance);
    expect_clean("1-entry transaction");

    /* 16 entries in account order 3, 10, 1, 8, ... (7 is coprime to 16: each account once).
     * Accounts 2k and 2k+1 form a zero-sum pair: -(10 + 2k) and +(10 + 2k). */
    for (size_t i = 0; i < N; i++) {
        size_t a = (7 * i + 3) % N;
        long amount = (long)(10 + a - a % 2);

        entries[i] = (transaction_entry_t){&g_accounts[a], a % 2 == 0 ? -amount : amount};
    }
    CHECK(checked_apply(entries, N) == true, "apply_transaction() returned false for a funded %d-account transaction", N);
    for (size_t i = 0; i < N; i++) {
        long want = 100 + entries[i].delta;

        CHECK(entries[i].account->balance == want,
              "account %u holds %ld, expected %ld — its delta was not applied exactly once",
              entries[i].account->id, entries[i].account->balance, want);
    }
    expect_clean("16-account transaction");

    /* 16 entries again, with account SHORT short of funds and LAST in entry order: every
     * other account deposits k, SHORT withdraws 15k > its balance. Nothing may change. */
    for (size_t i = 0; i < N; i++)
        before[i] = g_accounts[i].balance;

    long k = before[SHORT] / (N - 1) + 1;
    size_t n = 0;

    for (size_t i = 0; i < N; i++) {
        size_t a = (5 * i + 1) % N;

        if (a != SHORT)
            entries[n++] = (transaction_entry_t){&g_accounts[a], k};
    }
    entries[n++] = (transaction_entry_t){&g_accounts[SHORT], -(long)(N - 1) * k};

    CHECK(checked_apply(entries, n) == false, "apply_transaction() returned true although account %u holds %ld and its delta is -%ld",
          g_accounts[SHORT].id, before[SHORT], (long)(N - 1) * k);
    for (size_t i = 0; i < N; i++) {
        if (g_accounts[i].balance != before[i]) {
            CHECK(false, "PARTIAL application: the transaction failed, but account %u went from %ld "
                  "to %ld",
                  g_accounts[i].id, before[i], g_accounts[i].balance);
            break;
        }
    }
    expect_clean("16-account transaction with one short account");
    bank_teardown();
}

/* ---- shuffled_three_accounts ------------------------------------------------------------------
 * Ids 20, 10, 30 at ascending addresses; two transactions over the same three accounts with
 * differently shuffled entry lists. Single-threaded. */

static void test_shuffled_three_accounts(void)
{
    static account_t acc[3]; /* ids 20, 10, 30 at ascending addresses */
    const unsigned ids[3] = {20, 10, 30};

    lock_check_reset();
    g_naccounts = 0;
    for (size_t i = 0; i < 3; i++)
        account_init(&acc[i], ids[i], 100);

    transaction_entry_t t1[] = {{&acc[0], -5}, {&acc[2], +5}, {&acc[1], 0}};
    transaction_entry_t t2[] = {{&acc[1], +7}, {&acc[0], -3}, {&acc[2], -4}};

    CHECK(checked_apply(t1, 3) == true, "apply_transaction() returned false for a funded 3-account transaction");
    expect_clean("ids not in address order, entries shuffled one way");
    CHECK(checked_apply(t2, 3) == true, "apply_transaction() returned false for a funded 3-account transaction");
    expect_clean("ids not in address order, entries shuffled the other way");
    CHECK(acc[0].balance == 92 && acc[1].balance == 107 && acc[2].balance == 101,
          "balances are %ld, %ld, %ld, expected 92, 107, 101", acc[0].balance, acc[1].balance,
          acc[2].balance);
    for (size_t i = 0; i < 3; i++)
        account_destroy(&acc[i]);
}

/* ---- overlapping_reversed_concurrent: the ABBA probe, N accounts wide ----------------------- */

typedef struct {
    bool reversed;
    unsigned iters;
} overlap_worker_args_t;

static void *overlap_worker(void *arg)
{
    overlap_worker_args_t *w = arg;

    for (unsigned it = 0; it < w->iters; it++) {
        transaction_entry_t entries[6];

        for (size_t i = 0; i < 6; i++) {
            size_t idx = w->reversed ? 5 - i : i;

            entries[i].account = &g_accounts[idx];
            entries[i].delta = (idx % 2 == 0) ? -1 : +1;
        }
        checked_apply(entries, 6);
    }
    return NULL;
}

static void test_overlapping_reversed_concurrent(void)
{
    enum { THREADS = 4 };
    unsigned ids[6];
    long balances[6];
    pthread_t tids[THREADS];
    overlap_worker_args_t args[THREADS];
    unsigned iters = 10000 * hw_stress_scale();

    for (size_t i = 0; i < 6; i++) {
        ids[i] = (unsigned)(i + 1);
        balances[i] = 1000000;
    }
    bank_setup(ids, balances, 6);
    for (unsigned t = 0; t < THREADS; t++) {
        args[t] = (overlap_worker_args_t){.reversed = t % 2 == 1, .iters = iters};
        if (pthread_create(&tids[t], NULL, overlap_worker, &args[t]) != 0)
            hw_die("cannot create worker threads");
    }
    for (unsigned t = 0; t < THREADS; t++)
        pthread_join(tids[t], NULL);

    CHECK(bank_total(g_accounts, 6) == 6000000,
          "total money changed under contention: %ld, expected 6000000",
          bank_total(g_accounts, 6));
    CHECK_INVARIANT(lock_check_violations() == 0,
                    "%lu locking-protocol violation(s) while 4 threads ran transactions over the "
                    "same 6 accounts (see the INVARIANT lines above)",
                    lock_check_violations());
    bank_teardown();
}

/* ---- drain_race: many threads race to withdraw the LAST unit -------------------------------- */

typedef struct {
    course_gate_t *arrived; /* workers count themselves in here ...            */
    course_gate_t *go;      /* ... and main fires this once ALL have arrived   */
    bool succeeded;
} drain_worker_args_t;

static void *drain_worker(void *arg)
{
    drain_worker_args_t *w = arg;
    transaction_entry_t entries[] = {{&g_accounts[0], -1}, {&g_accounts[1], +1}};

    course_gate_add(w->arrived, 1);  /* I am at the starting line */
    course_gate_wait_min(w->go, 1);  /* released into apply_transaction() together */
    w->succeeded = checked_apply(entries, 2);
    return NULL;
}

static void test_drain_race(void)
{
    enum { THREADS = 8 };
    const unsigned ids[] = {1, 2};
    const long balances[] = {1, 0};
    unsigned rounds = 5 * hw_stress_scale();

    for (unsigned r = 0; r < rounds && !hw_test_failed(); r++) {
        pthread_t tids[THREADS];
        drain_worker_args_t args[THREADS];
        course_gate_t arrived, go;
        unsigned successes = 0;

        bank_setup(ids, balances, 2);
        /* Delay every lock() to widen the race window (see lock_check.h). */
        lock_check_set_acquire_delay_us(1000);
        course_gate_init(&arrived);
        course_gate_init(&go);
        for (unsigned t = 0; t < THREADS; t++) {
            args[t] = (drain_worker_args_t){.arrived = &arrived, .go = &go};
            if (pthread_create(&tids[t], NULL, drain_worker, &args[t]) != 0)
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
              "round %u: %u of %d concurrent transactions withdrew account 1's last unit "
              "(expected exactly 1): the balance check and the withdrawal were not one "
              "indivisible step", r, successes,
              THREADS);
        CHECK_INVARIANT(g_accounts[0].balance >= 0, "round %u: account 1 overdrawn to %ld", r,
                        g_accounts[0].balance);
        CHECK_INVARIANT(lock_check_violations() == 0, "round %u: %lu protocol violation(s)",
                        r, lock_check_violations());
        course_gate_destroy(&go);
        course_gate_destroy(&arrived);
        bank_teardown();
    }
}

/* ---- total_conserved_stress: random overlapping transactions -------------------------------- */

typedef struct {
    unsigned iters;
    uint64_t seed;
} random_txn_args_t;

static void *random_txn_worker(void *arg)
{
    random_txn_args_t *w = arg;
    course_rng_t rng;

    size_t naccounts = g_naccounts; /* snapshot: constant while workers run */

    if (naccounts < 8 || naccounts > MAX_ACCOUNTS) /* k below needs up to 8 distinct picks */
        return NULL;
    course_rng_seed(&rng, w->seed);
    for (unsigned it = 0; it < w->iters; it++) {
        size_t idx[MAX_ACCOUNTS];
        transaction_entry_t entries[Q1_MAX_ENTRIES];
        size_t k = 2 + 2 * course_rng_below(&rng, 4); /* 2, 4, 6 or 8 distinct accounts */

        for (size_t i = 0; i < naccounts; i++)
            idx[i] = i;
        for (size_t i = 0; i < k; i++) { /* partial Fisher-Yates: k distinct picks */
            size_t j = i + course_rng_below(&rng, (uint32_t)(naccounts - i));
            size_t tmp = idx[i];

            idx[i] = idx[j];
            idx[j] = tmp;
        }
        for (size_t i = 0; i < k; i += 2) { /* zero-sum pairs, order as shuffled */
            long amount = (long)course_rng_below(&rng, 40);

            entries[i] = (transaction_entry_t){&g_accounts[idx[i]], -amount};
            entries[i + 1] = (transaction_entry_t){&g_accounts[idx[i + 1]], +amount};
        }
        checked_apply(entries, k);
    }
    return NULL;
}

static void test_total_conserved_stress(void)
{
    enum { THREADS = 8, ACCOUNTS = 12 };
    unsigned ids[ACCOUNTS];
    long balances[ACCOUNTS];
    pthread_t tids[THREADS];
    random_txn_args_t args[THREADS];
    unsigned iters = 10000 * hw_stress_scale();

    for (size_t i = 0; i < ACCOUNTS; i++) {
        ids[i] = (unsigned)(5 * (i + 1));
        balances[i] = 1000;
    }
    bank_setup(ids, balances, ACCOUNTS);
    for (unsigned t = 0; t < THREADS; t++) {
        args[t] = (random_txn_args_t){.iters = iters, .seed = 0xC0FFEE00 + t};
        if (pthread_create(&tids[t], NULL, random_txn_worker, &args[t]) != 0)
            hw_die("cannot create worker threads");
    }
    for (unsigned t = 0; t < THREADS; t++)
        pthread_join(tids[t], NULL);

    CHECK(bank_total(g_accounts, ACCOUNTS) == 1000L * ACCOUNTS,
          "total money changed: %ld, expected %ld", bank_total(g_accounts, ACCOUNTS),
          1000L * ACCOUNTS);
    for (size_t i = 0; i < ACCOUNTS; i++)
        CHECK_INVARIANT(g_accounts[i].balance >= 0, "account %u went negative (%ld)",
                        g_accounts[i].id, g_accounts[i].balance);
    CHECK_INVARIANT(lock_check_violations() == 0, "%lu locking-protocol violation(s)",
                    lock_check_violations());
    bank_teardown();
}

/* ---- runner ------------------------------------------------------------------------------------ */

static const hw_test_t TESTS[] = {
    {"trace_session", test_trace_session,
     "replays trace/session.trace; exact balances from trace/session.answer", 0},
    {"two_account_regression", test_two_account_regression,
     "2-entry transaction with the entries in descending ID order; all-or-nothing", 0},
    {"n_account_arbitrary_order", test_n_account_arbitrary_order,
     "one 8-account transaction with reversed entries: exact result", 0},
    {"max_entries", test_max_entries,
     "1 entry, then 16 scrambled entries (the maximum): exact result; one short account changes nothing", 0},
    {"shuffled_three_accounts", test_shuffled_three_accounts,
     "ids 20/10/30 at ascending addresses: two transactions with shuffled entries, exact result", 0},
    {"overlapping_reversed_concurrent", test_overlapping_reversed_concurrent,
     "4 threads, same 6 accounts, half with reversed entry lists: deadlock probe + checker",
     0},
    {"drain_race", test_drain_race,
     "8 threads race for the last unit under a forced interleaving: exactly one succeeds", 0},
    {"total_conserved_stress", test_total_conserved_stress,
     "8 threads, random zero-sum transactions over 12 accounts: money conserved", 0},
};

int main(int argc, char **argv)
{
    return hw_run_tests("Quiz Q1 (" HW_IMPL_NAME ") — atomic N-account transaction", argc,
                        argv, TESTS, sizeof TESTS / sizeof TESTS[0]);
}
