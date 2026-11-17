/* instrument.h — harness introspection over the choke points (instructor supplied).
 *
 * Two layers of visibility, neither of which touches your code:
 *   - x86.c: every acquisition/release flows through the provided xchg(), so the harness
 *     tracks lock-word holders, counts spins, and reports lock misuse on FIRST occurrence;
 *   - diagnosis_pre()/diagnosis_post(): checkpoints in the sem_wait/sem_post frames of the
 *     student file (REQUIRED for grading — see src/sem_pc.c). They let the checker track
 *     which semaphores each thread holds and report semaphore-protocol violations on their
 *     first occurrence.
 */
#ifndef COURSE_INSTRUMENT_H
#define COURSE_INSTRUMENT_H

#include "types.h"

#include <stdbool.h>
#include <stddef.h>

struct sem;

/* ---- x86.c: the xchg choke point --------------------------------------------------------- */

void lockstat_reset(void);                   /* zero counters/registry (between tests) */
unsigned long lockstat_violations(void);     /* protocol violations since reset */
unsigned long lockstat_spins(void);          /* failed 1->1 exchanges (spin iterations) */
unsigned long lockstat_acquisitions(void);   /* successful 0->1 transitions */
bool lockstat_held_by_me(volatile uint *addr);   /* does the CALLING thread hold it? */
bool lockstat_held_by_any(volatile uint *addr);  /* does anyone hold it? */

/* ---- src/diagnosis.c: the semaphore-protocol checkpoints ---------------------------------- */

/* Called by the instructor frames in src/sem_pc.c around YOUR sem_wait_impl/sem_post_impl.
 * Student code must never call these (the submission screen rejects it). */
enum diag_op { DIAG_SEM_WAIT, DIAG_SEM_POST };
void diagnosis_pre(struct sem *s, enum diag_op op);
void diagnosis_post(struct sem *s, enum diag_op op);

/* Instructor side. */
void diag_reset(void);                  /* zero counters/roles (between tests)            */
unsigned long diag_violations(void);    /* protocol violations since reset                */
void diag_declare(struct sem *s, const char *role, bool is_mutex); /* bb_create calls it  */
/* bb_create brackets the student's bb_init with this; sem_init on a DECLARED semaphore
 * outside that window (from put/get) resets a counter or frees a held mutex, and is
 * reported on its first occurrence. */
void diag_init_phase(bool inside_bb_init);
bool diag_in_init_phase(void);
/* Where the CALLING thread is in the student code, e.g. "in get() → sem_wait_impl(&full) →
 * acquire()": the harness-bracketed put()/get() call, the sem_wait/sem_post frame in progress,
 * and `lockfn` ("acquire"/"release", or NULL). Writes into buf and returns it. */
const char *diag_where(char *buf, size_t n, const char *lockfn);
size_t diag_held_by_me(void);           /* mutex-role sems the CALLING thread holds (0/1) */
/* The semaphore's value read under its internal lock (through xchg, so the read is ordered
 * with the student's writes). The checkpoints report a negative value — see diagnosis.c. */
int diag_sem_value(struct sem *s);

/* Per-call protocol window: the harness brackets every put()/get() call (on the calling
 * thread). At end: the call must not still hold the mutex semaphore, and must have taken
 * it at least once (buffer/fill/use need mutual exclusion). */
void diag_call_begin(const char *fn);
void diag_call_end(void);

/* Report through the same counter/diagnostic path (first MAX_REPORTS are printed). */
void diagnosis_violation(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
