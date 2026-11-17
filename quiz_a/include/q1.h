/* q1.h — Quiz Q1 framework (instructor supplied; do not edit).
 *
 * A transaction touches N distinct accounts at once and must be atomic — all deltas applied,
 * or none. Every account has its own lock, and many threads run transactions concurrently.
 */
#ifndef COURSE_Q1_H
#define COURSE_Q1_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

#define Q1_MAX_ENTRIES 16

typedef struct {
    unsigned id; /* unique and immutable */
    long balance;
    pthread_mutex_t lock;
} account_t;

typedef struct {
    account_t *account;
    long delta; /* positive: deposit; negative: withdrawal */
} transaction_entry_t;

/* THE ONLY way student code takes and releases an account's lock. Every call flows through
 * the harness (src/lock_check.c), which checks the locking protocol at runtime.
 * lock() blocks until the lock is held; there is no trylock in this protocol. */
void lock(account_t *account);
void unlock(account_t *account);

/* THE quiz function (src/transaction.c). Many threads call it CONCURRENTLY — there is no
 * outer lock; the accounts' own locks are the only synchronization.
 * Preconditions guaranteed by every caller:
 *   - 1 <= count <= Q1_MAX_ENTRIES;
 *   - the entries reference DISTINCT accounts with unique immutable IDs;
 *   - the deltas sum to zero; no arithmetic overflow; all locks are initialized;
 *   - the entries arrive in ARBITRARY order — do not assume they are sorted.
 * Lock EVERY involved account (one whose delta is 0 included) before checking any balance,
 * and decide success or failure only while holding all of them.
 * Returns true after applying every delta. Returns false — with NO account changed — if any
 * resulting balance would go negative. */
bool apply_transaction(transaction_entry_t entries[], size_t count);

/* ---- instructor harness ----------------------------------------------------------------- */

/* ERRORCHECK mutexes: a double lock reports an error inside lock() instead of hanging the
 * quiz machine. account_init also registers the account with the lock checker. */
void account_init(account_t *account, unsigned id, long balance);
void account_destroy(account_t *account);

/* Snapshot of the total, for QUIESCENT points only (no transaction in flight): each balance
 * is read under its own lock, never two nested, so the harness takes no part in the
 * locking protocol. */
long bank_total(account_t *accounts, size_t naccounts);

#endif /* COURSE_Q1_H */
