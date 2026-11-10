/* check.c — see check.h */
#define _XOPEN_SOURCE 700 /* sigaltstack / SA_ONSTACK are XSI: not visible under bare POSIX */
#include "check.h"
#include "course_runtime.h"

#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static _Atomic unsigned g_failures;          /* CHECK failures so far (any thread)          */
static _Atomic unsigned g_failures_at_start; /* value when the current test began           */
static _Atomic unsigned g_late_failures;     /* raised by threads an earlier test abandoned  */
static _Atomic bool g_runner_finished;       /* set before hw_run_tests returns (exit guard) */
static unsigned g_stress_scale = 1;

void hw_fail(const char *kind, const char *file, int line, const char *fmt, ...)
{
    char msg[1024];
    va_list ap;

    va_start(ap, fmt);
    int n = vsnprintf(msg, sizeof msg, fmt, ap);

    va_end(ap);
    if (n < 0)
        snprintf(msg, sizeof msg, "(unprintable diagnostic)");
    else if ((size_t)n >= sizeof msg)
        memcpy(msg + sizeof msg - sizeof " [truncated]", " [truncated]", sizeof " [truncated]");

    if (course_thread_is_stale()) {
        /* A thread abandoned by an EARLIER test woke up and failed a check. Charging it to
         * the test running now would be wrong; print it, flag it, keep the run failed. */
        atomic_fetch_add(&g_late_failures, 1);
        course_line_break_if_open();
        printf("    LATE %s at %s:%d (raised by a thread an earlier test abandoned; not charged "
               "to the current test): %s\n", kind, file, line, msg);
        return;
    }
    atomic_fetch_add(&g_failures, 1);
    course_line_break_if_open();
    printf("    %s at %s:%d: %s\n", kind, file, line, msg);
}

void hw_die(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fputs("FATAL: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    fflush(stdout);
    _exit(COURSE_EXIT_FRAMEWORK); /* _exit: the exit guard below must not fire a second FATAL */
}

bool hw_test_failed(void)
{
    return atomic_load(&g_failures) != atomic_load(&g_failures_at_start);
}

/* A test body (or code it calls) that calls exit(0) would end the process with a PASSING
 * status and no verdict line. The guard turns that into a framework failure. Registered
 * after the platform gate (which legitimately exits 125 on its own). */
static void exit_guard(void)
{
    if (atomic_load(&g_runner_finished))
        return;
    static const char msg[] =
        "FATAL: the process called exit() before the test runner finished — no verdict was "
        "reached. Nothing in a test or in student code may call exit().\n";
    ssize_t ignored = write(STDERR_FILENO, msg, sizeof msg - 1);

    (void)ignored;
    _exit(COURSE_EXIT_FRAMEWORK);
}

unsigned hw_stress_scale(void)
{
    return g_stress_scale;
}

/* Skeletons fail loudly with abort() and broken ring arithmetic can crash;
 * translate both into a readable hint. Async-signal-safe: write() only, then re-raise so the
 * exit status keeps the original signal. */
static void fatal_signal_handler(int sig)
{
    static const char abort_msg[] =
        "\nABORT: the implementation called abort(). An unimplemented TODO in the student "
        "file fails loudly by design — implement it, REMOVE the abort() line, then rerun.\n";
    static const char crash_msg[] =
        "\nCRASH: the implementation accessed invalid memory or math (or overflowed its "
        "stack — unbounded recursion?). Check the ring index arithmetic (wraparound?), then "
        "rerun the *_addr-test binary (AddressSanitizer) for an exact report.\n";
    const char *msg = sig == SIGABRT ? abort_msg : crash_msg;
    size_t len = sig == SIGABRT ? sizeof abort_msg - 1 : sizeof crash_msg - 1;
    ssize_t ignored = write(STDERR_FILENO, msg, len);

    (void)ignored;
    /* SA_RESETHAND already restored the default disposition: re-raise so the exit status
     * still carries the original signal. */
    raise(sig);
}

/* The handler runs on an alternate stack (SA_ONSTACK) so that a stack OVERFLOW (unbounded
 * recursion in student code) still produces the CRASH hint instead of faulting again inside
 * the handler. sigaction is process-wide; the alternate stack is PER THREAD: the main
 * thread's is installed here, task threads get their own in course_runtime.c. Threads a
 * test creates with raw pthread_create have none and lose only the hint, not the exit
 * status. */
static void install_crash_handlers(void)
{
    struct sigaction sa;
    const int sigs[] = {SIGABRT, SIGSEGV, SIGBUS, SIGFPE, SIGILL};

    course_thread_altstack_enable();
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = fatal_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_ONSTACK | SA_RESETHAND;
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
        sigaction(sigs[i], &sa, NULL);
}

static const hw_test_t *find_test(const hw_test_t *tests, size_t n, const char *name)
{
    for (size_t i = 0; i < n; i++) {
        if (strcmp(tests[i].name, name) == 0)
            return &tests[i];
    }
    return NULL;
}

static unsigned env_unsigned(const char *name, unsigned fallback)
{
    const char *env = getenv(name); /* NOLINT(concurrency-mt-unsafe) — single-threaded here */
    long v = env && *env ? strtol(env, NULL, 10) : 0;

    return v >= 1 ? (unsigned)v : fallback;
}

/* Set by each module's Makefile: which of the three builds this binary is (basic / addr /
 * thread), the two sibling binaries the basic build should remind the student to run, and
 * the module's watchdog default (the quizzes use a shorter one). */
#ifndef HW_VARIANT
#define HW_VARIANT ""
#endif
#ifndef HW_SIBLINGS
#define HW_SIBLINGS ""
#endif
#ifndef COURSE_TEST_TIMEOUT_DEFAULT_S
#define COURSE_TEST_TIMEOUT_DEFAULT_S 30
#endif

static const char *variant_desc(void)
{
    if (strcmp(HW_VARIANT, "basic") == 0)
        return "plain build";
    if (strcmp(HW_VARIANT, "addr") == 0)
        return "AddressSanitizer + UndefinedBehaviorSanitizer build";
    if (strcmp(HW_VARIANT, "thread") == 0)
        return "ThreadSanitizer build";
    return NULL;
}

int hw_run_tests(const char *banner, int argc, char **argv, const hw_test_t *tests,
                 size_t ntests)
{
    unsigned passed = 0, failed = 0;
    unsigned default_timeout;

    setvbuf(stdout, NULL, _IOLBF, 0); /* TEST lines must reach the log before a possible hang */
    course_require_supported_platform();
    atexit(exit_guard);
    install_crash_handlers();
    printf("# %s%s%s%s\n", banner, variant_desc() ? "  [" : "", variant_desc() ? variant_desc() : "",
           variant_desc() ? "]" : "");

    g_stress_scale = env_unsigned("HW_STRESS_SCALE", 1);
    default_timeout = env_unsigned("COURSE_TEST_TIMEOUT_S", COURSE_TEST_TIMEOUT_DEFAULT_S);
    (void)course_settle_ms(); /* prime the cached value while the process is single-threaded */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0) {
            for (size_t j = 0; j < ntests; j++)
                printf("%-32s %s\n", tests[j].name, tests[j].doc);
            atomic_store(&g_runner_finished, true);
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("usage: %s [--list] [test-name ...]\n", argv[0]);
            atomic_store(&g_runner_finished, true);
            return 0;
        }
        if (!find_test(tests, ntests, argv[i])) {
            fprintf(stderr, "%s: unknown test '%s' (see --list)\n", argv[0], argv[i]);
            atomic_store(&g_runner_finished, true);
            return COURSE_EXIT_FRAMEWORK;
        }
    }

    for (size_t j = 0; j < ntests; j++) {
        const hw_test_t *t = &tests[j];

        if (argc > 1) {
            bool selected = false;
            for (int i = 1; i < argc; i++) {
                if (strcmp(argv[i], t->name) == 0)
                    selected = true;
            }
            if (!selected)
                continue;
        }

        /* The name reaches the log BEFORE the test runs, so a watchdog timeout or crash is
         * attributable; a clean pass closes the same line as "TEST name ... OK". */
        printf("TEST %-32s ... ", t->name);
        fflush(stdout);
        course_line_mark_open();
        course_generation_advance();
        course_watchdog_start(t->timeout_s ? t->timeout_s : default_timeout, t->name);
        atomic_store(&g_failures_at_start, atomic_load(&g_failures));
        unsigned abandoned_before = course_tasks_abandoned();
        uint64_t t0 = course_now_ns();
        t->fn();
        double ms = (double)(course_now_ns() - t0) / 1e6;
        course_watchdog_disarm();
        if (hw_test_failed()) {
            failed++;
            course_line_break_if_open();
            printf("TEST %s: FAIL (%.0f ms)\n", t->name, ms);
        } else {
            passed++;
            if (course_line_consume_open()) /* nothing interrupted the line: finish it */
                printf("OK (%.0f ms)\n", ms);
            else
                printf("TEST %s: OK (%.0f ms)\n", t->name, ms);
        }
        unsigned abandoned = course_tasks_abandoned() - abandoned_before;

        if (abandoned)
            printf("    note: %u thread(s) started by this test are stuck and were abandoned; "
                   "they stay alive, and anything they print later is tagged LATE\n", abandoned);
    }

    unsigned late = atomic_load(&g_late_failures);

    if (late)
        printf("    note: %u LATE diagnostic(s) came from threads abandoned by earlier tests\n",
               late);
    if (failed == 0 && late == 0) {
        printf("ALL TESTS OK (%u/%u)\n", passed, passed + failed);
        if (strcmp(HW_VARIANT, "basic") == 0 && HW_SIBLINGS[0] != '\0')
            printf("(basic build only — also run %s before submitting: the grader runs all "
                   "three)\n", HW_SIBLINGS);
    } else
        printf("TESTS FAILED: %u of %u failed%s\n", failed, passed + failed,
               late ? " (plus LATE diagnostics)" : "");
    atomic_store(&g_runner_finished, true);
    return failed || late ? COURSE_EXIT_FAIL : COURSE_EXIT_PASS;
}
