/* check.h — minimal test framework shared by every module of this course (instructor code).
 *
 * A test is a void function that records failures with CHECK()/CHECK_INVARIANT(); it keeps
 * running after a failed check so one run reports every broken property. The runner prints one
 * line per test — the name goes out BEFORE the body runs, so a hang is attributable:
 *     TEST name ... OK (ms)     |     TEST name: FAIL (ms)   + diagnostic lines
 * and the diagnostic lines distinguish four outcome classes:
 *     "    WRONG-ANSWER at file:line: ..."   result differs from the defined answer
 *     "    INVARIANT at file:line: ..."      a structural property was violated
 *     "TIMEOUT: ..." + exit 124              watchdog fired: probable deadlock (evidence!)
 *     "UNSUPPORTED: ..." + exit 125          the environment cannot run this module
 * Every test runs under the portable in-process watchdog (course_runtime.h); no test can hang
 * the run forever, on any supported platform.
 */
#ifndef COURSE_CHECK_H
#define COURSE_CHECK_H

#include <stdbool.h>
#include <stddef.h>

typedef void (*hw_test_fn)(void);

typedef struct hw_test {
    const char *name; /* [a-z0-9_]+ — selectable on the command line: ./test_x name ... */
    hw_test_fn fn;
    const char *doc;    /* one line: what the test drives and which mistake it catches */
    unsigned timeout_s; /* watchdog for this test; 0 = default (COURSE_TEST_TIMEOUT_S or 30) */
} hw_test_t;

/* Record a failure in the running test (thread-safe; does not abort the test). */
void hw_fail(const char *kind, const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/* The message is mandatory: say which property broke and why it matters. */
#define CHECK(cond, ...)                                                                       \
    do {                                                                                       \
        if (!(cond))                                                                           \
            hw_fail("WRONG-ANSWER", __FILE__, __LINE__, __VA_ARGS__);                          \
    } while (0)

#define CHECK_INVARIANT(cond, ...)                                                             \
    do {                                                                                       \
        if (!(cond))                                                                           \
            hw_fail("INVARIANT", __FILE__, __LINE__, __VA_ARGS__);                             \
    } while (0)

/* Abort the whole run — for framework misuse (cannot create threads, ...), never as an oracle. */
void hw_die(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

/* Has the currently running test recorded a failure yet? */
bool hw_test_failed(void);

/* HW_STRESS_SCALE environment variable (default 1): multiplies stress iteration counts. */
unsigned hw_stress_scale(void);

/* Print `banner`, check the platform, then run the tests named in argv (all if none; --list
 * prints names and docs). Call it from main(); returns the process exit code. Installs the
 * crash-hint signal handlers (on an alternate stack) and an exit() guard: a test or student
 * code that calls exit() before the runner finishes ends the process as a framework failure
 * (exit 2) instead of a silent pass. */
int hw_run_tests(const char *banner, int argc, char **argv, const hw_test_t *tests,
                 size_t ntests);

#endif /* COURSE_CHECK_H */
