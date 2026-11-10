/* instrument.h — harness introspection over the choke points (instructor supplied).
 *
 * Everything student code can call is provided by the harness — sem_init/sem_wait/sem_post
 * (src/sem.c) — so the tests observe your semaphore protocol without touching your code:
 * which semaphore a thread is waiting on, whether it holds the mutex semaphore meanwhile,
 * and whether every put()/get() call took and released the mutex. Misuse is reported on its
 * FIRST occurrence with a diagnostic that names the lecture concept it violates.
 */
#ifndef COURSE_INSTRUMENT_H
#define COURSE_INSTRUMENT_H

#include "sem.h"

/* ---- src/sem.c: the semaphore choke point ------------------------------------------------- */

void semstat_reset(void);                /* zero counters (between tests)                   */
unsigned long semstat_violations(void);  /* protocol violations since reset                 */
int semstat_value(struct sem *s);        /* current value (-1 if never initialized); read
                                          * under the internal lock — meaningful only at
                                          * quiescent points (no put/get in flight)         */

/* Report a violation through the same counter/diagnostic path sem.c uses (first
 * MAX_REPORTS occurrences are printed as "    INVARIANT: ..." lines). */
void semstat_violation(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Registration: bb_create() declares each semaphore's role BEFORE student bb_init() runs,
 * so diagnostics can say "empty"/"full"/"mutex" and the mutex gets hold-tracking. */
void sem_prepare(struct sem *s, const char *role, bool is_mutex);

/* bb_create() brackets the student's bb_init() with this: sem_init on a prepared (role-
 * bearing) semaphore is legal only inside that window. A sem_init from put()/get() would
 * silently reset a counter — or free a mutex another thread holds — and is reported on its
 * first occurrence. */
void semstat_init_phase(bool inside_bb_init);

/* Per-call protocol window: the harness brackets every put()/get() call (on the calling
 * thread) with begin/end. At end, two first-occurrence checks fire:
 *   - the call returned while still HOLDING the mutex semaphore (leaked lock), or
 *   - the call never took the mutex at all (buffer/fill/use touched without mutual
 *     exclusion — the fill race of lecture 9). */
void semstat_call_begin(const char *fn);
void semstat_call_end(void);

#endif /* COURSE_INSTRUMENT_H */
