/* instrument.h — harness introspection over the choke point (instructor supplied).
 *
 * The one function student code can call is provided by the harness — xchg() (x86.c) — so
 * the tests observe your lock protocol without touching your code: xchg's free->held and
 * held->free transitions reveal the holder of every lock, busy returns are counted as
 * spins, and misuse (releasing a lock you do not hold, re-acquiring one you do, a release
 * that bypassed xchg) is reported on its FIRST occurrence with a diagnostic.
 */
#ifndef COURSE_INSTRUMENT_H
#define COURSE_INSTRUMENT_H

#include "types.h"

#include <stdbool.h>

/* ---- x86.c: the xchg choke point --------------------------------------------------------- */

void lockstat_reset(void);                   /* zero counters/registry (between tests) */
unsigned long lockstat_violations(void);     /* protocol violations since reset */
unsigned long lockstat_spins(void);          /* busy returns (exchanges that found the lock held) */
unsigned long lockstat_acquisitions(void);   /* successful free->held transitions */
bool lockstat_held_by_me(volatile uint *addr);   /* does the CALLING thread hold it? */
bool lockstat_held_by_any(volatile uint *addr);  /* does anyone hold it? */

#endif
