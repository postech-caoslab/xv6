/* lock_check.h — runtime lock-ordering checker for the ordered-mutex modules (instructor code).
 *
 * The lock()/unlock() wrappers (q1.h) are the choke point: every acquisition and release in
 * student code flows through src/lock_check.c, which keeps a per-thread list of held account
 * locks and flags, deterministically on the very first wrong call — no race or unlucky
 * schedule needed:
 *
 *   - lock-order inconsistency: the first acquisition that contradicts an order some thread
 *     used earlier (a cycle in the held-before graph);
 *   - double lock: locking an account this thread already holds (lock() is not recursive);
 *   - bad unlock: unlocking an account this thread does not hold;
 *   - and, via the per-call acquisition counter below, an apply_transaction() that returned
 *     without having locked every involved account.
 *
 * Harness code takes account mutexes directly (pthread), so its own locking is invisible
 * here. Account mutexes are PTHREAD_MUTEX_ERRORCHECK, so even a real double lock returns an
 * error instead of hanging the test.
 */
#ifndef COURSE_LOCK_CHECK_H
#define COURSE_LOCK_CHECK_H

#include "q1.h"

#include <stddef.h>

/* Forget all registrations and zero the violation counter (call at the start of a test). */
void lock_check_reset(void);

/* Declare `account` to the checker (called by account_init, before any student code runs;
 * registration is not thread-safe by design). */
void lock_check_register(account_t *account);

/* Total protocol violations recorded since the last reset (any thread). */
unsigned long lock_check_violations(void);

/* Deterministic-interleaving control: make every lock() sleep `us` before touching the
 * mutex. Tests use it to widen the race windows between threads deterministically. Reset
 * clears it. */
void lock_check_set_acquire_delay_us(unsigned us);

/* Number of registered locks the CALLING thread currently holds — checked after every
 * apply_transaction() call. */
size_t lock_check_held_by_me(void);

/* Successful lock() acquisitions by the CALLING thread since it started. Tests diff this
 * around an apply_transaction() call. */
unsigned long lock_check_my_acquisitions(void);

/* Most registered locks the CALLING thread held AT THE SAME TIME since the last reset. Tests
 * reset it before an apply_transaction() call and compare it with the entry count. */
void lock_check_reset_my_peak(void);
size_t lock_check_my_peak(void);

#endif /* COURSE_LOCK_CHECK_H */
