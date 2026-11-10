/* course_runtime.c — see course_runtime.h. Instructor code. */
#define _XOPEN_SOURCE 700 /* sigaltstack / SS_DISABLE are XSI: not visible under bare POSIX */
#ifdef __APPLE__
#define _DARWIN_C_SOURCE /* expose _SC_NPROCESSORS_ONLN alongside the POSIX/XSI interfaces */
#endif
#ifdef __linux__
#define _GNU_SOURCE /* sched_getaffinity + CPU_COUNT: course_ncpus honours the affinity mask */
#endif
#include "course_runtime.h"

#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sched.h>
#endif

/* ---- platform gate ------------------------------------------------------------------------ */

void course_require_supported_platform(void)
{
    static _Atomic unsigned probe;

    if (!atomic_is_lock_free(&probe)) {
        fprintf(stderr,
                "UNSUPPORTED: atomic words are not hardware-lock-free on this platform; "
                "this module cannot run here\n");
        exit(COURSE_EXIT_UNSUPPORTED);
    }
}

/* ---- test-output line state ----------------------------------------------------------------
 * The test runner prints "TEST name ... " and leaves the line open so a clean pass ends as
 * "TEST name ... OK". Anything that wants to print a whole line first calls
 * course_line_break_if_open() so diagnostics never land mid-line. */

static _Atomic bool g_line_open;

void course_line_mark_open(void)
{
    atomic_store(&g_line_open, true);
}

bool course_line_consume_open(void)
{
    return atomic_exchange(&g_line_open, false);
}

void course_line_break_if_open(void)
{
    if (course_line_consume_open()) {
        putchar('\n');
        fflush(stdout);
    }
}

/* ---- time ---------------------------------------------------------------------------------- */

uint64_t course_now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

uint64_t course_cpu_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

void course_sleep_ms(unsigned ms)
{
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L};

    nanosleep(&ts, NULL);
}

void course_sleep_us(unsigned us)
{
    struct timespec ts = {.tv_sec = us / 1000000, .tv_nsec = (long)(us % 1000000) * 1000L};

    nanosleep(&ts, NULL);
}

unsigned course_settle_ms(void)
{
    /* The runner primes this before any test thread exists; the atomic makes even a first
     * call from two threads benign (both compute and store the same value). */
    static _Atomic unsigned cached;
    unsigned v = atomic_load(&cached);

    if (v == 0) {
        const char *env = getenv("COURSE_SETTLE_MS"); /* NOLINT(concurrency-mt-unsafe) */
        long n = env && *env ? strtol(env, NULL, 10) : 0;

        v = (n >= 1 && n <= 60000) ? (unsigned)n : 100;
        atomic_store(&cached, v);
    }
    return v;
}

long course_ncpus(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);

    if (n < 1)
        n = 1;
#ifdef __linux__
    /* sysconf reports the machine, not what THIS process may use. Honour an affinity mask
     * (taskset, some VMs) and a cgroup v2 CPU quota (docker --cpus), whichever is smaller;
     * both are best-effort reads and fall back to the sysconf value. */
    cpu_set_t set;

    if (sched_getaffinity(0, sizeof set, &set) == 0) {
        long a = CPU_COUNT(&set);

        if (a >= 1 && a < n)
            n = a;
    }
    long quota = -1, period = 0;
    FILE *fp = fopen("/sys/fs/cgroup/cpu.max", "r"); /* cgroup v2: "max 100000" or "q p" */

    if (fp) {
        char q[32];

        /* NOLINTNEXTLINE(cert-err34-c) — a kernel-formatted file; a mismatch just skips */
        if (fscanf(fp, "%31s %ld", q, &period) == 2 && strcmp(q, "max") != 0)
            quota = strtol(q, NULL, 10);
        fclose(fp);
    } else { /* cgroup v1: two files, quota -1 = unlimited */
        static const char *const v1[][2] = {
            {"/sys/fs/cgroup/cpu/cpu.cfs_quota_us", "/sys/fs/cgroup/cpu/cpu.cfs_period_us"},
            {"/sys/fs/cgroup/cpu,cpuacct/cpu.cfs_quota_us",
             "/sys/fs/cgroup/cpu,cpuacct/cpu.cfs_period_us"},
        };

        for (size_t i = 0; i < sizeof v1 / sizeof v1[0] && quota < 0; i++) {
            FILE *fq = fopen(v1[i][0], "r"), *fpd = fopen(v1[i][1], "r");

            /* NOLINTNEXTLINE(cert-err34-c) */
            if (fq && fpd && fscanf(fq, "%ld", &quota) == 1 && fscanf(fpd, "%ld", &period) != 1)
                quota = -1;
            if (fq)
                fclose(fq);
            if (fpd)
                fclose(fpd);
        }
    }
    if (quota > 0 && period > 0) {
        long c = (quota + period - 1) / period; /* ceil(quota / period) */

        if (c >= 1 && c < n)
            n = c;
    }
#endif
    return n;
}

/* ---- RNG ----------------------------------------------------------------------------------- */

void course_rng_seed(course_rng_t *rng, uint64_t seed)
{
    rng->s = seed ? seed : 0x9E3779B97F4A7C15ull;
}

uint64_t course_rng_next(course_rng_t *rng)
{
    uint64_t x = rng->s;

    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng->s = x;
    return x * 0x2545F4914F6CDD1Dull;
}

uint32_t course_rng_below(course_rng_t *rng, uint32_t bound)
{
    return bound ? (uint32_t)(course_rng_next(rng) % bound) : 0;
}

/* ---- items --------------------------------------------------------------------------------- */

static uint64_t item_checksum(unsigned producer_id, unsigned sequence)
{
    /* splitmix64 finalizer over the identity: any single-bit corruption changes the checksum */
    uint64_t z = ((uint64_t)producer_id << 32 | sequence) + 0x9E3779B97F4A7C15ull;

    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

item_t item_make(unsigned producer_id, unsigned sequence)
{
    item_t it = {.producer_id = producer_id,
                 .sequence = sequence,
                 .payload_check = item_checksum(producer_id, sequence)};

    return it;
}

bool item_payload_ok(item_t item)
{
    return item.payload_check == item_checksum(item.producer_id, item.sequence);
}

item_t item_poison(void)
{
    return item_make(UINT_MAX, UINT_MAX);
}

bool item_is_poison(item_t item)
{
    return item.producer_id == UINT_MAX && item.sequence == UINT_MAX;
}

/* ---- logs ---------------------------------------------------------------------------------- */

void item_log_init(item_log_t *log)
{
    log->items = NULL;
    log->len = 0;
    log->cap = 0;
}

void item_log_append(item_log_t *log, item_t item)
{
    if (log->len == log->cap) {
        size_t ncap = log->cap ? log->cap * 2 : 256;
        item_t *p = realloc(log->items, ncap * sizeof *p);

        if (!p) {
            fprintf(stderr, "FATAL: out of memory growing an item log\n");
            exit(COURSE_EXIT_FRAMEWORK);
        }
        log->items = p;
        log->cap = ncap;
    }
    log->items[log->len++] = item;
}

void item_log_destroy(item_log_t *log)
{
    free(log->items);
    item_log_init(log);
}

/* ---- verification -------------------------------------------------------------------------- */

/* Cap the per-defect diagnostics so a totally broken queue prints pages, not gigabytes. */
#define VERIFY_MAX_REPORTS 10

bool verify_exactly_once(const item_log_t *logs, size_t nlogs, unsigned nproducers,
                         unsigned per_producer)
{
    size_t expected = (size_t)nproducers * per_producer;
    unsigned char *seen = calloc(expected ? expected : 1, 1);
    size_t total = 0;
    unsigned reports = 0;
    bool ok = true;

    if (!seen) {
        fprintf(stderr, "FATAL: out of memory in verify_exactly_once\n");
        exit(COURSE_EXIT_FRAMEWORK);
    }

#define REPORT(...)                                                                            \
    do {                                                                                       \
        ok = false;                                                                            \
        if (reports++ < VERIFY_MAX_REPORTS) {                                                  \
            course_line_break_if_open();                                                       \
            printf("    INVARIANT: " __VA_ARGS__);                                             \
        }                                                                                      \
    } while (0)

    for (size_t c = 0; c < nlogs; c++) {
        for (size_t i = 0; i < logs[c].len; i++) {
            item_t it = logs[c].items[i];

            if (item_is_poison(it)) {
                REPORT("consumer log %zu contains a poison item — harness items must be "
                       "filtered before verification\n", c);
                continue;
            }
            if (!item_payload_ok(it)) {
                REPORT("corrupt item in consumer log %zu: (p%u, #%u) has a wrong checksum — "
                       "the queue tore or overwrote a slot\n", c, it.producer_id, it.sequence);
                continue;
            }
            if (it.producer_id >= nproducers || it.sequence >= per_producer) {
                REPORT("fabricated item in consumer log %zu: (p%u, #%u) was never produced\n",
                       c, it.producer_id, it.sequence);
                continue;
            }
            size_t idx = (size_t)it.producer_id * per_producer + it.sequence;
            if (seen[idx]) {
                REPORT("duplicated item: (p%u, #%u) consumed more than once — one slot was "
                       "handed to two consumers\n", it.producer_id, it.sequence);
                continue;
            }
            seen[idx] = 1;
            total++;
        }
    }

    if (total != expected) {
        size_t shown = 0;
        REPORT("lost items: %zu of %zu produced items were never consumed\n",
               expected - total, expected);
        for (size_t idx = 0; idx < expected && shown < VERIFY_MAX_REPORTS; idx++) {
            if (!seen[idx]) {
                course_line_break_if_open();
                printf("        missing: (p%zu, #%zu)\n", idx / per_producer,
                       idx % per_producer);
                shown++;
            }
        }
    }
#undef REPORT

    free(seen);
    return ok;
}

bool verify_exact_fifo(const item_log_t *log, unsigned producer_id, unsigned count)
{
    unsigned reports = 0;
    bool ok = true;

    if (log->len != count) {
        course_line_break_if_open();
        printf("    WRONG-ANSWER: consumed %zu items, expected exactly %u\n", log->len, count);
        ok = false;
    }
    for (size_t i = 0; i < log->len; i++) {
        item_t it = log->items[i];
        bool good = it.producer_id == producer_id && it.sequence == i && item_payload_ok(it);

        if (!good && reports++ < VERIFY_MAX_REPORTS) {
            course_line_break_if_open();
            printf("    WRONG-ANSWER: position %zu holds (p%u, #%u), expected (p%u, #%zu) — "
                   "FIFO order broken\n", i, it.producer_id, it.sequence, producer_id, i);
        }
        ok &= good;
    }
    return ok;
}

bool verify_per_producer_order(const item_log_t *log, unsigned nproducers,
                               unsigned per_producer)
{
    unsigned *next = calloc(nproducers, sizeof *next);
    unsigned reports = 0;
    bool ok = true;

    if (!next) {
        fprintf(stderr, "FATAL: out of memory in verify_per_producer_order\n");
        exit(COURSE_EXIT_FRAMEWORK);
    }
    for (size_t i = 0; i < log->len; i++) {
        item_t it = log->items[i];

        if (it.producer_id >= nproducers || !item_payload_ok(it)) {
            if (reports++ < VERIFY_MAX_REPORTS)
                course_line_break_if_open(),
                printf("    INVARIANT: bad item (p%u, #%u) in the consumer log\n",
                       it.producer_id, it.sequence);
            ok = false;
            continue;
        }
        if (it.sequence != next[it.producer_id]) {
            if (reports++ < VERIFY_MAX_REPORTS)
                course_line_break_if_open(),
                printf("    WRONG-ANSWER: producer %u's items out of order: got #%u, expected "
                       "#%u — a FIFO queue must preserve each producer's enqueue order\n",
                       it.producer_id, it.sequence, next[it.producer_id]);
            ok = false;
            continue;
        }
        next[it.producer_id]++;
    }
    for (unsigned p = 0; p < nproducers; p++) {
        if (next[p] != per_producer) {
            if (reports++ < VERIFY_MAX_REPORTS)
                course_line_break_if_open(),
                printf("    INVARIANT: producer %u: consumed %u of %u items\n", p, next[p],
                       per_producer);
            ok = false;
        }
    }
    free(next);
    return ok;
}

/* ---- watchdog ------------------------------------------------------------------------------ */

/* One detached thread polls a monotonic deadline every 100 ms. Polling (instead of a timed
 * condvar wait) sidesteps the fact that macOS lacks pthread_condattr_setclock: there is no
 * portable CLOCK_MONOTONIC condvar wait, and a watchdog only needs 0.1 s resolution. */
static struct {
    pthread_mutex_t mu;
    bool thread_started;
    bool armed;
    uint64_t deadline_ns;
    unsigned seconds;
    const char *what;
} wd = {.mu = PTHREAD_MUTEX_INITIALIZER};

static void *watchdog_main(void *arg)
{
    (void)arg;
    for (;;) {
        course_sleep_ms(100);
        pthread_mutex_lock(&wd.mu);
        bool fired = wd.armed && course_now_ns() >= wd.deadline_ns;
        unsigned seconds = wd.seconds;
        const char *what = wd.what;
        pthread_mutex_unlock(&wd.mu);
        if (fired) {
            /* No stdio from here on: a wedged thread could be holding a stream lock, and
             * the watchdog must never wait on anything. Diagnostic lines are already
             * line-flushed; only the open "TEST name ... " prefix needs its newline. */
            char buf[512];
            int n = snprintf(buf, sizeof buf,
                             "TIMEOUT: '%s' still not finished after %u s — probable deadlock "
                             "or livelock. A timeout is evidence, not proof: rerun under a "
                             "debugger and look at what every thread is waiting for.\n",
                             what, seconds);

            if (course_line_consume_open()) {
                ssize_t ignored = write(STDOUT_FILENO, "\n", 1);

                (void)ignored;
            }
            if (n > 0) {
                ssize_t ignored = write(STDERR_FILENO, buf, (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1);

                (void)ignored;
            }
            _exit(COURSE_EXIT_TIMEOUT);
        }
    }
    return NULL;
}

void course_watchdog_start(unsigned seconds, const char *what)
{
    pthread_mutex_lock(&wd.mu);
    if (!wd.thread_started) {
        pthread_t t;
        pthread_attr_t attr;

        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&t, &attr, watchdog_main, NULL) != 0) {
            fprintf(stderr, "FATAL: cannot create the watchdog thread\n");
            exit(COURSE_EXIT_FRAMEWORK);
        }
        pthread_attr_destroy(&attr);
        wd.thread_started = true;
    }
    wd.armed = true;
    wd.seconds = seconds;
    wd.deadline_ns = course_now_ns() + (uint64_t)seconds * 1000000000ull;
    wd.what = what;
    pthread_mutex_unlock(&wd.mu);
}

void course_watchdog_disarm(void)
{
    pthread_mutex_lock(&wd.mu);
    wd.armed = false;
    pthread_mutex_unlock(&wd.mu);
}

/* ---- one-shot tasks -------------------------------------------------------------------------- */

/* ---- test generations (see course_runtime.h) ---------------------------------------------- */

static _Atomic unsigned g_generation = 1;
static _Thread_local unsigned t_generation; /* 0 = unstamped (main, or a raw pthread) */

void course_generation_advance(void)
{
    atomic_fetch_add(&g_generation, 1);
}

bool course_thread_is_stale(void)
{
    return t_generation != 0 && t_generation != atomic_load(&g_generation);
}

/* ---- per-thread alternate signal stack ---------------------------------------------------- */

#define ALTSTACK_BYTES (64 * 1024) /* comfortably above MINSIGSTKSZ on every target */

static _Thread_local void *t_altstack;

void course_thread_altstack_enable(void)
{
    if (t_altstack)
        return;
    t_altstack = malloc(ALTSTACK_BYTES);
    if (!t_altstack)
        return; /* best effort: without it only the CRASH hint is lost, never the status */
    stack_t ss = {.ss_sp = t_altstack, .ss_size = ALTSTACK_BYTES, .ss_flags = 0};

    if (sigaltstack(&ss, NULL) != 0) {
        free(t_altstack);
        t_altstack = NULL;
    }
}

void course_thread_altstack_disable(void)
{
    if (!t_altstack)
        return;
    stack_t ss = {.ss_sp = NULL, .ss_size = 0, .ss_flags = SS_DISABLE};

    sigaltstack(&ss, NULL);
    free(t_altstack);
    t_altstack = NULL;
}

struct course_task {
    pthread_t tid;
    _Atomic bool done;
    void *(*fn)(void *);
    void *arg;
    unsigned generation; /* the test that started this task */
};

static _Atomic unsigned g_tasks_abandoned;

static void *task_trampoline(void *arg)
{
    course_task_t *t = arg;

    t_generation = t->generation;
    course_thread_altstack_enable();
    t->fn(t->arg);
    course_thread_altstack_disable();
    atomic_store(&t->done, true); /* t is never freed while this thread can reach here */
    return NULL;
}

course_task_t *course_task_start(void *(*fn)(void *), void *arg)
{
    course_task_t *t = malloc(sizeof *t);

    if (!t) {
        fprintf(stderr, "FATAL: out of memory in course_task_start\n");
        exit(COURSE_EXIT_FRAMEWORK);
    }
    atomic_init(&t->done, false);
    t->fn = fn;
    t->arg = arg;
    t->generation = atomic_load(&g_generation);
    if (pthread_create(&t->tid, NULL, task_trampoline, t) != 0) {
        fprintf(stderr, "FATAL: cannot create a task thread\n");
        exit(COURSE_EXIT_FRAMEWORK);
    }
    return t;
}

bool course_task_done(const course_task_t *task)
{
    return atomic_load(&task->done);
}

bool course_task_await(course_task_t *task, unsigned ms)
{
    uint64_t deadline = course_now_ns() + (uint64_t)ms * 1000000ull;

    while (!course_task_done(task)) {
        if (course_now_ns() >= deadline)
            return false;
        course_sleep_ms(1);
    }
    pthread_join(task->tid, NULL);
    free(task);
    return true;
}

void course_task_abandon(course_task_t *task)
{
    /* Deliberately no free(): the detached thread will store task->done if it ever
     * finishes. The record stays reachable from that thread's frame (so LeakSanitizer is
     * quiet) and is reclaimed at process exit. */
    pthread_detach(task->tid);
    atomic_fetch_add(&g_tasks_abandoned, 1);
}

unsigned course_tasks_abandoned(void)
{
    return atomic_load(&g_tasks_abandoned);
}

/* ---- gate ---------------------------------------------------------------------------------- */

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    unsigned value;
} gate_impl_t;

void course_gate_init(course_gate_t *gate)
{
    gate_impl_t *g = malloc(sizeof *g);

    if (!g) {
        fprintf(stderr, "FATAL: out of memory in course_gate_init\n");
        exit(COURSE_EXIT_FRAMEWORK);
    }
    pthread_mutex_init(&g->mu, NULL);
    pthread_cond_init(&g->cv, NULL);
    g->value = 0;
    gate->impl = g;
}

void course_gate_destroy(course_gate_t *gate)
{
    gate_impl_t *g = gate->impl;

    pthread_mutex_destroy(&g->mu);
    pthread_cond_destroy(&g->cv);
    free(g);
    gate->impl = NULL;
}

void course_gate_set(course_gate_t *gate, unsigned value)
{
    gate_impl_t *g = gate->impl;

    pthread_mutex_lock(&g->mu);
    if (value > g->value)
        g->value = value;
    pthread_cond_broadcast(&g->cv);
    pthread_mutex_unlock(&g->mu);
}

void course_gate_add(course_gate_t *gate, unsigned n)
{
    gate_impl_t *g = gate->impl;

    pthread_mutex_lock(&g->mu);
    g->value += n;
    pthread_cond_broadcast(&g->cv);
    pthread_mutex_unlock(&g->mu);
}

unsigned course_gate_get(course_gate_t *gate)
{
    gate_impl_t *g = gate->impl;

    pthread_mutex_lock(&g->mu);
    unsigned v = g->value;
    pthread_mutex_unlock(&g->mu);
    return v;
}

void course_gate_wait_min(course_gate_t *gate, unsigned value)
{
    gate_impl_t *g = gate->impl;

    pthread_mutex_lock(&g->mu);
    while (g->value < value)
        pthread_cond_wait(&g->cv, &g->mu);
    pthread_mutex_unlock(&g->mu);
}
