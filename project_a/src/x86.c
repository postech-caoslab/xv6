/* x86.c — portable xchg + the lock-protocol checker behind it (instructor supplied).
 *
 * Every acquisition and release in this module flows through xchg(), so the harness can
 * track, WITHOUT touching student code:
 *   - who holds which lock word (the thread whose exchange returned 0 for a non-zero newval);
 *   - spin counts (exchanges that returned non-zero for a non-zero newval);
 *   - protocol violations, reported on their FIRST occurrence:
 *       releasing a lock that is not held / held by another thread,
 *       spinning on a lock the same thread already holds (self-deadlock).
 * The bookkeeping is race-free by construction: the lock protocol itself serializes the
 * free->held and held->free transitions of each word.
 *
 * Value model (xv6's holding() semantics): 0 = free, non-zero = held. newval 0 is a release,
 * any other newval an acquire attempt, and a returned 0 means the attempt won. The hardware
 * exchange is value-agnostic, and so is this bookkeeping; the course protocol writes exactly
 * 1 for "held", and THAT is checked by the tests (locked == 1 after an acquire), not here —
 * so an unusual constant gets a precise test message instead of a misleading one from the
 * checker. The primitive is for lock words only: every address exchanged through here is
 * registered and tracked as a lock.
 */
#include "course_runtime.h"
#include "instrument.h"
#include "x86.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>

/* __atomic_exchange_n is not declared in any header: it is a COMPILER BUILT-IN (GCC's
 * memory-model-aware atomic builtins, implemented identically by Clang/Apple Clang — the
 * two compiler families this course supports). The compiler emits the native instruction
 * inline: `xchg` on x86-64 (exactly xv6's instruction), `swpal` or an ldaxr/stlxr loop on
 * ARM64. This assert makes the build FAIL on any target where a 4-byte exchange would not
 * be native lock-free hardware — so if it compiles, xchg() really is one atomic step. */
_Static_assert(__atomic_always_lock_free(sizeof(uint), 0),
               "UNSUPPORTED: 4-byte atomic exchange is not a native instruction here");

#define MAX_LOCKS 64
#define MAX_REPORTS 10
/* Oversubscription relief: every YIELD_EVERY-th FAILED exchange (process-wide count) calls
 * sched_yield() before returning. With more spinning threads than CPUs, a holder that was
 * preempted mid-critical-section otherwise gets the CPU back only when a spinner's timeslice
 * expires (milliseconds per handoff: Quiz 2's fleets took 51-63 s on two CPUs, 7 s on four);
 * the yield hands it back in microseconds (2-3 s on two CPUs). On an idle CPU sched_yield
 * returns at once, so the primitive keeps its meaning — the exchange has already happened,
 * the old value is returned unchanged, and a spinner still burns its CPU (Homework A's
 * spin-cost measurement is unaffected: ~600 ms CPU, ~27 M busy exchanges). */
#define YIELD_EVERY 64

static struct lockent {
    volatile uint *addr; /* published by the registry count store below */
    _Atomic int owner;   /* tid, or -1 when free */
} g_locks[MAX_LOCKS];
static _Atomic unsigned g_nlocks;
static pthread_mutex_t g_registry_mu = PTHREAD_MUTEX_INITIALIZER;

static _Atomic unsigned long g_spins, g_acquisitions, g_violations;
/* Set once a lock word was found 0 while the checker still recorded a holder: the previous
 * release bypassed xchg(). From then on the owner field can be stale, so the "spinning on a
 * lock you already hold" check (which reads it) is muted — it would only mislead. */
static _Atomic bool g_release_bypassed_xchg;
static _Atomic int g_next_tid = 1;
static _Thread_local int t_tid; /* 0 = unassigned */

static int mytid(void)
{
    if (t_tid == 0)
        t_tid = atomic_fetch_add(&g_next_tid, 1);
    return t_tid;
}

static void violation(const char *msg)
{
    if (atomic_fetch_add(&g_violations, 1) < MAX_REPORTS) {
        course_line_break_if_open();
        printf("    INVARIANT: %s\n", msg);
    }
}

static struct lockent *lookup(const volatile uint *addr)
{
    unsigned n = atomic_load(&g_nlocks);

    for (unsigned i = 0; i < n; i++) {
        if (g_locks[i].addr == addr)
            return &g_locks[i];
    }
    return NULL;
}

static struct lockent *lookup_or_add(volatile uint *addr)
{
    struct lockent *e = lookup(addr);

    if (e)
        return e;
    pthread_mutex_lock(&g_registry_mu);
    e = lookup(addr);
    if (!e) {
        unsigned n = atomic_load(&g_nlocks);

        if (n < MAX_LOCKS) {
            g_locks[n].addr = addr;
            atomic_store(&g_locks[n].owner, -1);
            atomic_store(&g_nlocks, n + 1); /* publish after the fields are set */
            e = &g_locks[n];
        }
    }
    pthread_mutex_unlock(&g_registry_mu);
    return e;
}

uint xchg(volatile uint *addr, uint newval)
{
    struct lockent *e = lookup_or_add(addr);
    int me = mytid();

    /* Release bookkeeping must happen BEFORE the exchange: while the word is still held, the
     * owner field is stable (spinners never write it, and the next acquirer cannot win the
     * word yet). Clearing it after the exchange would race the next winner's owner store. */
    if (e && newval == 0) {
        int owner = atomic_load(&e->owner);

        if (owner == -1)
            violation("release of a spinlock that is not held — a double release, or did "
                      "the acquire side return without its exchange winning (old value 0)?");
        else if (owner != me)
            violation("release of a spinlock this thread does not hold — only the holder "
                      "may release");
        /* Whatever the reason, the word is about to read 0: forget the holder now, or the
         * next winner would be blamed for a release that bypassed xchg. */
        atomic_store(&e->owner, -1);
    }

    uint old = __atomic_exchange_n(addr, newval, __ATOMIC_SEQ_CST);

    if (e && newval != 0) {
        if (old == 0) {
            /* Only the winning exchange runs this. A correct release clears the owner
             * (above) BEFORE its exchange stores 0, so a winner must find -1 here; a stale
             * holder means the word went to 0 some other way — a plain store. */
            int prev = atomic_exchange(&e->owner, me);

            if (prev != -1) {
                atomic_store(&g_release_bypassed_xchg, true);
                violation("lock word read 0 although the checker still saw a holder — the "
                          "previous release stored 0 WITHOUT xchg(). A plain store bypasses "
                          "the atomic protocol (and is a data race: the *_thread-test build "
                          "reports it); release through xchg(&lk->locked, 0)");
            }
            atomic_fetch_add(&g_acquisitions, 1);
        } else {
            if ((atomic_fetch_add(&g_spins, 1) % YIELD_EVERY) == YIELD_EVERY - 1)
                sched_yield();
            if (atomic_load(&e->owner) == me && !atomic_load(&g_release_bypassed_xchg))
                violation("spinning on a spinlock this thread ALREADY holds — "
                          "self-deadlock: nobody can release it while you spin");
        }
    }
    return old;
}

/* ---- introspection ------------------------------------------------------------------------ */

void lockstat_reset(void)
{
    /* called between tests, when no student threads are live */
    atomic_store(&g_nlocks, 0);
    atomic_store(&g_spins, 0);
    atomic_store(&g_acquisitions, 0);
    atomic_store(&g_violations, 0);
    atomic_store(&g_release_bypassed_xchg, false);
}

unsigned long lockstat_violations(void)
{
    return atomic_load(&g_violations);
}

unsigned long lockstat_spins(void)
{
    return atomic_load(&g_spins);
}

unsigned long lockstat_acquisitions(void)
{
    return atomic_load(&g_acquisitions);
}

bool lockstat_held_by_me(volatile uint *addr)
{
    struct lockent *e = lookup(addr);

    return e && atomic_load(&e->owner) == mytid();
}

bool lockstat_held_by_any(volatile uint *addr)
{
    struct lockent *e = lookup(addr);

    return e && atomic_load(&e->owner) != -1;
}
