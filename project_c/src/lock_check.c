/* lock_check.c — lock()/unlock() + the ordering checker behind them (see lock_check.h). */
#include "course_runtime.h"
#include "lock_check.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MAX_REGISTERED 256 /* accounts per test */
#define MAX_HELD 64        /* locks one thread may hold at once (Q1_MAX_ENTRIES fits) */
#define MAX_REPORTS 10     /* diagnostics printed per reset; the counter keeps counting */

/* ---- the held-before graph ------------------------------------------------------------------
 * Deadlock freedom needs ONE fixed total order over the locks, shared by every thread; which
 * order is irrelevant (ascending id, descending id, address, any fixed permutation). So the
 * checker does not prescribe an order: it records every observation "held B while acquiring
 * A" as an edge B -> A, process-wide, and reports the first acquisition that closes a cycle —
 * the moment two threads are provably using different orders (the lockdep idea). Nodes are
 * registry indexes; the graph is a bit matrix guarded by one mutex. */
static unsigned char g_before[MAX_REGISTERED][MAX_REGISTERED]; /* g_before[b][a]: b held while a acquired */
static pthread_mutex_t g_graph_mu = PTHREAD_MUTEX_INITIALIZER;

static account_t *registry[MAX_REGISTERED];
static size_t nregistered; /* written only during single-threaded setup, then read-only */

static _Atomic unsigned long g_violations;
static _Atomic unsigned g_acquire_delay_us;

void lock_check_set_acquire_delay_us(unsigned us)
{
    atomic_store(&g_acquire_delay_us, us);
}

static void acquire_delay(void)
{
    unsigned us = atomic_load(&g_acquire_delay_us);

    if (us) {
        struct timespec ts = {.tv_sec = us / 1000000, .tv_nsec = (long)(us % 1000000) * 1000L};

        nanosleep(&ts, NULL);
    }
}

static _Thread_local account_t *held[MAX_HELD];
static _Thread_local size_t nheld;
static _Thread_local unsigned long acquisitions;

static void violation(const char *what, unsigned a, unsigned b)
{
    unsigned long n = atomic_fetch_add(&g_violations, 1);

    if (n < MAX_REPORTS) {
        course_line_break_if_open();
        printf("    INVARIANT: ");
        printf(what, a, b); /* NOLINT(format-nonliteral) — all format strings are local */
        printf("\n");
        fflush(stdout);
    }
}

static const char *const MSG_ORDER =
    "lock-order inconsistency: this thread acquires account lock #%u while holding #%u, but an "
    "earlier acquisition (on this or another thread) took them the other way round. All threads "
    "must take the locks in ONE fixed order — any order, but the same one — or two of them can "
    "circular-wait (ABBA)";
static const char *const MSG_DOUBLE =
    "double lock: this thread already holds account lock #%u — a self-transfer must not lock "
    "the same nonrecursive lock twice%.0u";
static const char *const MSG_FOREIGN =
    "lock()/unlock() on something that is not a registered account — only the accounts the "
    "harness passed to transfer() may be locked%.0u%.0u";
static const char *const MSG_UNLOCK =
    "unlocking account lock #%u, which this thread does not hold%.0u";

void lock_check_reset(void)
{
    nregistered = 0;
    atomic_store(&g_violations, 0);
    atomic_store(&g_acquire_delay_us, 0);
    memset(g_before, 0, sizeof g_before);
    /* held/nheld are thread-local; tests reset between runs when no student thread is live,
     * and the main thread's list is empty whenever student code is not on its stack. */
}

void lock_check_register(account_t *account)
{
    if (nregistered == MAX_REGISTERED) {
        fprintf(stderr, "FATAL: lock_check: too many registered locks\n");
        return;
    }
    registry[nregistered++] = account;
}

unsigned long lock_check_violations(void)
{
    return atomic_load(&g_violations);
}

size_t lock_check_held_by_me(void)
{
    return nheld;
}

unsigned long lock_check_my_acquisitions(void)
{
    return acquisitions;
}

static int registry_index(const account_t *account)
{
    for (size_t i = 0; i < nregistered; i++) {
        if (registry[i] == account)
            return (int)i;
    }
    return -1;
}

static bool is_registered(const account_t *account)
{
    return registry_index(account) >= 0;
}

/* Is `to` reachable from `from` in the held-before graph? (BFS; graph mutex held.) */
static bool reaches(int from, int to)
{
    unsigned char seen[MAX_REGISTERED] = {0};
    int queue[MAX_REGISTERED], head = 0, tail = 0;

    queue[tail++] = from;
    seen[from] = 1;
    while (head < tail) {
        int n = queue[head++];

        if (n == to)
            return true;
        for (int m = 0; m < (int)nregistered; m++) {
            if (g_before[n][m] && !seen[m]) {
                seen[m] = 1;
                queue[tail++] = m;
            }
        }
    }
    return false;
}

/* Record "held `held_idx` while acquiring `acq_idx`"; report the first edge that closes a
 * cycle, i.e. the first time a path acq -> ... -> held already exists. */
static void note_order(int held_idx, int acq_idx, unsigned acq_id, unsigned held_id)
{
    pthread_mutex_lock(&g_graph_mu);
    bool cycle = !g_before[held_idx][acq_idx] && reaches(acq_idx, held_idx);

    g_before[held_idx][acq_idx] = 1;
    pthread_mutex_unlock(&g_graph_mu);
    if (cycle)
        violation(MSG_ORDER, acq_id, held_id);
}

/* ---- the choke points: the only lock/unlock student code can call -------------------------- */

void lock(account_t *account)
{
    bool known = is_registered(account);

    if (!known) {
        violation(MSG_FOREIGN, 0, 0);
    } else {
        bool twice = false;

        for (size_t i = 0; i < nheld && !twice; i++)
            twice = held[i] == account;
        if (twice) {
            violation(MSG_DOUBLE, account->id, 0); /* errorcheck mutex errors below, no hang */
        } else {
            int acq = registry_index(account);

            for (size_t i = 0; i < nheld; i++)
                note_order(registry_index(held[i]), acq, account->id, held[i]->id);
        }
    }
    acquire_delay();
    if (pthread_mutex_lock(&account->lock) == 0 && known) {
        if (nheld < MAX_HELD)
            held[nheld++] = account;
        acquisitions++;
    }
}

void unlock(account_t *account)
{
    if (is_registered(account)) {
        bool holding = false;

        for (size_t i = 0; i < nheld; i++) {
            if (held[i] == account) {
                holding = true;
                held[i] = held[nheld - 1]; /* order within the list does not matter */
                nheld--;
                break;
            }
        }
        if (!holding) {
            violation(MSG_UNLOCK, account->id, 0);
            return; /* unlocking a mutex we may not own is undefined — skip it */
        }
    }
    pthread_mutex_unlock(&account->lock);
}
