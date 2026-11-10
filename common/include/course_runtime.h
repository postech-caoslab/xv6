/* course_runtime.h — shared runtime for every module's harness (instructor supplied).
 *
 * Portable across Linux, macOS and WSL2: plain C11 + pthread mutexes/condvars + POSIX clocks.
 * No POSIX semaphores, no pthread_barrier_t, no OS-specific syscalls — the one Linux-only
 * read (the CPU-affinity mask and cgroup quota in course_ncpus) is guarded by #ifdef.
 */
#ifndef COURSE_RUNTIME_H
#define COURSE_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- process exit codes: scripts can tell the four failure classes apart ------------------- */
enum {
    COURSE_EXIT_PASS = 0,
    COURSE_EXIT_FAIL = 1,        /* wrong answer and/or invariant violation — see output lines */
    COURSE_EXIT_FRAMEWORK = 2,   /* harness misuse or setup failure (not a student outcome)    */
    COURSE_EXIT_TIMEOUT = 124,   /* watchdog fired: probable deadlock (evidence, not proof)    */
    COURSE_EXIT_UNSUPPORTED = 125, /* environment cannot run this module                       */
};

/* Exit COURSE_EXIT_UNSUPPORTED with a message if this platform cannot run the module
 * (atomic word not hardware-lock-free at runtime). Called by every test main. */
void course_require_supported_platform(void);

/* ---- time ------------------------------------------------------------------------------- */
uint64_t course_now_ns(void); /* monotonic wall clock */
uint64_t course_cpu_ns(void); /* CPU time consumed by the WHOLE process (all threads summed) */
void course_sleep_ms(unsigned ms);
void course_sleep_us(unsigned us); /* short pauses (forced interleavings); >= us, not exact */

/* How long a trace test waits for another thread to (probably) reach a blocking call.
 * COURSE_SETTLE_MS overrides the 100 ms default — raise it on a very loaded machine. Read
 * once (the runner primes it before any test thread exists) and cached atomically. */
unsigned course_settle_ms(void);

/* CPUs this process may actually run on (>= 1): online CPUs, capped on Linux by the affinity
 * mask and the cgroup CPU quota. Used only to size/skip/refuse spin experiments. */
long course_ncpus(void);

/* ---- deterministic RNG (xorshift64*) so failures reproduce across runs and machines ------ */
typedef struct {
    uint64_t s;
} course_rng_t;

void course_rng_seed(course_rng_t *rng, uint64_t seed);
uint64_t course_rng_next(course_rng_t *rng);
uint32_t course_rng_below(course_rng_t *rng, uint32_t bound); /* uniform in [0, bound) */

/* ---- items: unique identity + checksum makes every loss/dup/corruption
 *      detectable by set equality instead of sums ------------------------------------------ */
typedef struct {
    unsigned producer_id;
    unsigned sequence;
    uint64_t payload_check; /* derived from (producer_id, sequence); see item_make() */
} item_t;

item_t item_make(unsigned producer_id, unsigned sequence);
bool item_payload_ok(item_t item);

/* Poison items terminate consumer loops. They are created, enqueued and filtered ONLY by the
 * instructor harness (the termination protocol is not student work). */
item_t item_poison(void);
bool item_is_poison(item_t item);

/* ---- per-consumer logs: instructor-allocated; each consumer thread has
 *      exclusive ownership of one log until it is joined, then the main thread verifies ----- */
typedef struct {
    item_t *items;
    size_t len;
    size_t cap;
} item_log_t;

void item_log_init(item_log_t *log);
void item_log_append(item_log_t *log, item_t item);
void item_log_destroy(item_log_t *log);

/* ---- verification. All three print "    INVARIANT: ..." / "    WRONG-ANSWER: ..."
 *      diagnostics for each defect found and return the overall verdict. ------------------- */

/* Exactly-once MPMC integrity over all consumer logs: every (producer_id, sequence) with
 * producer_id < nproducers and sequence < per_producer appears exactly once, every checksum is
 * valid, nothing unexpected appears, and the total count matches. Order is NOT checked: with
 * concurrent consumers, per-consumer completion order does not reconstruct dequeue order. */
bool verify_exactly_once(const item_log_t *logs, size_t nlogs, unsigned nproducers,
                         unsigned per_producer);

/* Single-consumer exact FIFO: log must be exactly (producer_id, 0..count-1) in order. */
bool verify_exact_fifo(const item_log_t *log, unsigned producer_id, unsigned count);

/* MPSC order: filter the single consumer's log by producer; each subsequence must be
 * 0,1,2,... — i.e. every producer's items come out in the order that producer put them in. */
bool verify_per_producer_order(const item_log_t *log, unsigned nproducers,
                               unsigned per_producer);

/* ---- test-output line discipline: the runner leaves "TEST name ... " open so a clean pass
 *      ends as one line; ANY code printing a diagnostic line calls break_if_open() first ----- */
void course_line_mark_open(void);
void course_line_break_if_open(void);    /* if a line is open: newline + clear */
bool course_line_consume_open(void);     /* clear without printing; returns previous state */

/* ---- watchdog: turns a probable deadlock into a diagnosable exit instead of a silent hang.
 *      One process-wide watchdog thread; re-arming replaces the previous deadline. When it
 *      fires it uses write(2) + _exit only — never stdio, whose lock a wedged thread might
 *      hold. ------------------------------------------------------------------------------- */
void course_watchdog_start(unsigned seconds, const char *what);
void course_watchdog_disarm(void);

/* ---- one-shot tasks with a bounded wait: how trace tests observe blocking ------------------
 * A trace step like "begin C0 dequeue -> blocks" starts the call on its own thread, settles,
 * and checks it has NOT completed; a later step awaits completion with a deadline. There is no
 * portable pthread_timedjoin, so completion is a polled atomic flag. */
typedef struct course_task course_task_t;

course_task_t *course_task_start(void *(*fn)(void *), void *arg);
bool course_task_done(const course_task_t *task);
/* Poll until done or ms elapsed. true: task finished and was joined and freed (do not touch
 * the pointer again). false: still running — await again or abandon. */
bool course_task_await(course_task_t *task, unsigned ms);
/* Detach a stuck task. The record is NOT freed: the thread still owns it (it stores the done
 * flag when — if ever — it finishes), so freeing it here would be a use-after-free. The few
 * bytes stay reachable from that thread and are reclaimed at process exit; the test that
 * abandons a task has already failed and the process exits soon after. */
void course_task_abandon(course_task_t *task);
/* Tasks abandoned since process start — the runner reports a per-test delta so a reader knows
 * that threads from this test are still alive and may print late. */
unsigned course_tasks_abandoned(void);

/* ---- test generations: a thread abandoned by test N may wake during test N+1 and record a
 *      failure there. Every task thread is stamped with the generation of the test that
 *      started it; the runner advances the generation per test and hw_fail() asks whether the
 *      calling thread is stale, reporting such diagnostics as LATE instead of charging them
 *      to the wrong test. Threads the tests create directly (pthread_create) are unstamped and
 *      always count as current. --------------------------------------------------------- */
void course_generation_advance(void);
bool course_thread_is_stale(void);

/* ---- crash-hint support: give the CALLING thread a 64 KB alternate signal stack so the
 *      fatal-signal handler (check.c) survives a stack overflow on this thread. enable()
 *      allocates it; disable() frees it (call only when no handler can still be running on
 *      it, i.e. on the normal exit path of the thread). ---------------------------------- */
void course_thread_altstack_enable(void);
void course_thread_altstack_disable(void);

/* ---- gate: a monotonic counter + condvar, the portable rendezvous used by trace tests
 *      (pthread_barrier_t is not available on macOS; this is the course's replacement). ----- */
typedef struct course_gate {
    /* private */
    void *impl;
} course_gate_t;

void course_gate_init(course_gate_t *gate);
void course_gate_destroy(course_gate_t *gate);
void course_gate_set(course_gate_t *gate, unsigned value); /* value must not decrease */
void course_gate_add(course_gate_t *gate, unsigned n);
unsigned course_gate_get(course_gate_t *gate);
void course_gate_wait_min(course_gate_t *gate, unsigned value); /* block until gate >= value */

#endif /* COURSE_RUNTIME_H */
