// locks.c — YOUR file, and the ONLY file you submit for Homework A.
//
// It is xv6's spinlock.c minus the kernel-only parts (interrupt handling and debug
// bookkeeping). Edit only inside the marked regions; everything outside is
// discarded at grading time and replaced by the originals.
//
// Allowed inside the regions: xchg() and the fields of struct spinlock — nothing else.
// No pthreads, no <stdatomic.h>, no sleeping or yielding. The build's checker watches every
// xchg.

#include "spinlock.h"
#include "x86.h"

#include <stdlib.h>

// ======================= Mutual exclusion spin locks (xv6: spinlock.c) ======================

void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
void
acquire(struct spinlock *lk)
{
  (void)lk;

  /* === BEGIN STUDENT CODE: DO NOT EDIT OUTSIDE THIS REGION === */

  abort(); /* fails loudly on purpose — REMOVE this line after implementing your code */

  /* === END STUDENT CODE === */
}

// Release the lock.
void
release(struct spinlock *lk)
{
  (void)lk;

  /* === BEGIN STUDENT CODE: DO NOT EDIT OUTSIDE THIS REGION === */

  abort(); /* fails loudly on purpose — REMOVE this line after implementing your code */

  /* === END STUDENT CODE === */
}
