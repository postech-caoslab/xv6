/* transaction.c — Quiz Q1: YOUR file. Implement apply_transaction(); edit nothing else.
 *
 * Allowed inside the region: the functions declared in q1.h (lock()/unlock()),
 * local variables, and the fields of the structs.
 * Forbidden: pthread calls, extra/global locks, atomics, sleeping, dynamic allocation.
 * Every lock()/unlock() is protocol-checked at runtime.
 * Lock EVERY account in the transaction (delta 0 included) before checking any balance.
 */
#include "q1.h"

#include <stdlib.h>

bool apply_transaction(transaction_entry_t entries[], size_t count)
{
    (void)entries;
    (void)count;

    /* === BEGIN STUDENT CODE: DO NOT EDIT OUTSIDE THIS REGION === */

    abort(); /* fails loudly on purpose — REMOVE this line after implementing your code */

    /* === END STUDENT CODE === */
}
