/* bank.h — Homework C framework (instructor supplied; do not edit).
 *
 * Every account is protected by its OWN lock. A transfer updates two accounts as one atomic
 * operation, so it must hold two locks at once — which is exactly where nested locking creates
 * ordering constraints: two transfers in opposite directions that lock "mine first, then the
 * other one" form a circular wait (ABBA deadlock). The fix you implement is a TOTAL ORDER over
 * all locks: every transfer takes them in the SAME fixed order (ascending account id is the
 * natural choice; any fixed order works), whichever way the money flows.
 */
#ifndef COURSE_BANK_H
#define COURSE_BANK_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    unsigned id; /* unique and immutable */
    long balance;
    pthread_mutex_t lock; /* private to the harness — student code uses lock()/unlock() */
} account_t;

/* THE ONLY way student code takes and releases an account's lock. Every call flows through
 * the harness (src/lock_check.c), which reports protocol misuse on its FIRST occurrence:
 * taking two locks in an order that contradicts an earlier acquisition (any ONE fixed order
 * is fine, two different orders are not), locking an account this thread already holds,
 * unlocking one it does not hold, or finishing a transfer without having locked both
 * accounts. lock() blocks until the lock is held; there is no trylock in this protocol. */
void lock(account_t *account);
void unlock(account_t *account);

/* THE student function (transfer.c). Preconditions guaranteed by every caller:
 *   - `from` and `to` point to initialized accounts; `amount >= 0`;
 *   - distinct accounts have distinct IDs; `from == to` is legal (self-transfer).
 * Returns true after moving `amount` from `from` to `to`; returns false and changes NOTHING if
 * `from` has insufficient funds. A self-transfer changes nothing and returns true iff the
 * account holds at least `amount`. Check funds only after the locks are held: a balance read
 * without the lock can be stale by the time you act on it. */
bool transfer(account_t *from, account_t *to, long amount);

/* ---- instructor harness ---------------------------------------------------------------- */

/* Mutexes are created PTHREAD_MUTEX_ERRORCHECK: locking one twice from the same thread returns
 * an error inside lock() instead of hanging, so the tests can DIAGNOSE a double lock rather
 * than time out. account_init also registers the account with the order checker. */
void account_init(account_t *account, unsigned id, long balance);
void account_destroy(account_t *account);

/* Snapshot of the total, for QUIESCENT points only (no transfer in flight): each balance is
 * read under its own lock, never two locks nested, so the harness takes no part in the
 * lock-order graph. */
long bank_total(account_t *accounts, size_t naccounts);

#endif /* COURSE_BANK_H */
