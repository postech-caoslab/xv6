/* transfer.c — Homework C: YOUR file. Implement transfer(); edit nothing else.
 *
 * Allowed inside the region: lock()/unlock() on the two accounts, and the fields of
 * account_t. Forbidden: pthread calls, any extra or global lock, atomics, sleeping/yielding
 * (every lock()/unlock() is protocol-checked at runtime).
 */
#include "bank.h"

#include <stdlib.h>

bool transfer(account_t *from, account_t *to, long amount)
{
    (void)from;
    (void)to;
    (void)amount;

    /* === BEGIN STUDENT CODE: DO NOT EDIT OUTSIDE THIS REGION === */

    abort(); /* fails loudly on purpose — REMOVE this line after implementing your code */

    /* === END STUDENT CODE === */
}
