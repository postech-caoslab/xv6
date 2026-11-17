/* q1_harness.c — Quiz Q1 incidental code (instructor supplied; do not edit). */
#include "lock_check.h"
#include "q1.h"

#include <stdio.h>
#include <stdlib.h>

void account_init(account_t *account, unsigned id, long balance)
{
    pthread_mutexattr_t attr;

    account->id = id;
    account->balance = balance;
    if (pthread_mutexattr_init(&attr) != 0 ||
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK) != 0 ||
        pthread_mutex_init(&account->lock, &attr) != 0) {
        fprintf(stderr, "FATAL: cannot initialize an account mutex\n");
        exit(2);
    }
    pthread_mutexattr_destroy(&attr);
    lock_check_register(account); /* lock()/unlock() only accept registered accounts */
}

void account_destroy(account_t *account)
{
    pthread_mutex_destroy(&account->lock);
}

long bank_total(account_t *accounts, size_t naccounts)
{
    long total = 0;

    /* Called only at QUIESCENT points (every worker joined): each balance is read under its
     * own lock, one at a time, so the harness never takes part in the lock-order graph. */
    for (size_t i = 0; i < naccounts; i++) {
        pthread_mutex_lock(&accounts[i].lock);
        total += accounts[i].balance;
        pthread_mutex_unlock(&accounts[i].lock);
    }
    return total;
}
