/* spinlock.h — mutual exclusion spin lock, exactly as in xv6 (instructor supplied).
 *
 * Compare with xv6's spinlock.h: same struct, same functions. The kernel version carries two
 * extra debug fields (the owning cpu and a saved call stack) and acquire()/release() there
 * additionally disable interrupts (pushcli/popcli) — a kernel-only concern: an interrupt
 * handler that tried to take a lock its own CPU already holds would spin forever, and
 * userland threads have no interrupt handlers. Everything you implement here transfers 1:1.
 */
#ifndef COURSE_XV6_SPINLOCK_H
#define COURSE_XV6_SPINLOCK_H

#include "types.h"

// Mutual exclusion lock.
struct spinlock {
  uint locked;       // Is the lock held?  0 = free, 1 = held.

  // For debugging:
  char *name;        // Name of lock.
};

void initlock(struct spinlock *lk, char *name); // provided (outside the student markers)
void acquire(struct spinlock *lk);              // YOU implement (the student file)
void release(struct spinlock *lk);              // YOU implement (the student file)

#endif
