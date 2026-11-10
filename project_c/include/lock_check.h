/* lock_check.h — runtime lock-ordering checker for the ordered-mutex modules (instructor code).
 *
 * The lock()/unlock() wrappers (bank.h) are the choke point: every acquisition and release in
 * student code flows through src/lock_check.c, which keeps a per-thread list of held account
 * locks and flags, deterministically on the very first wrong call — no race or unlucky
 * schedule needed:
 *
 *   - lock-order inconsistency: the first acquisition that contradicts an order some thread
 *     used earlier (a cycle in the held-before graph) — ANY fixed total order is accepted,
 *     ascending id, descending id, address, ...; two different orders are not;
 *   - double lock: locking an account this thread already holds (the self-transfer mistake);
 *   - bad unlock: unlocking an account this thread does not hold;
 *   - and, via the per-call acquisition counter below, a transfer() that returned without
 *     having locked both accounts (single-lock or private/global-lock "solutions").
 *
 * Harness code takes account mutexes directly (pthread), so its own locking is invisible
 * here. Account mutexes are PTHREAD_MUTEX_ERRORCHECK, so even a real double lock returns an
 * error instead of hanging the test.
 */
#ifndef COURSE_LOCK_CHECK_H
#define COURSE_LOCK_CHECK_H

#include "bank.h"

#include <stddef.h>

/* Forget all registrations and zero the violation counter (call at the start of a test). */
void lock_check_reset(void);

/* Declare `account` to the checker (called by account_init, before any student code runs;
 * registration is not thread-safe by design). */
void lock_check_register(account_t *account);

/* Total protocol violations recorded since the last reset (any thread). */
unsigned long lock_check_violations(void);

/* Deterministic-interleaving control: make every lock() sleep `us` before touching the
 * mutex. Tests use it to stretch the window between "student code decided to lock" and
 * "student code holds the lock" so check-outside-the-lock bugs manifest on EVERY schedule
 * instead of one lucky one. Reset clears it. */
void lock_check_set_acquire_delay_us(unsigned us);

/* Number of registered locks the CALLING thread currently holds — must be 0 after every
 * transfer/apply_transaction returns ("release every acquired lock on every path"). */
size_t lock_check_held_by_me(void);

/* Successful lock() acquisitions by the CALLING thread since it started. Tests diff this
 * around a transfer() call: a two-account transfer must acquire at least 2 account locks
 * (a self-transfer at least 1) — 0 or 1 means the balances were read behind a private or
 * global lock (or none), which no other thread's protocol respects. */
unsigned long lock_check_my_acquisitions(void);

#endif /* COURSE_LOCK_CHECK_H */
