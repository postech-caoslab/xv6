/* x86.h — the ONE hardware primitive this module needs: atomic exchange
 * (instructor supplied; do not edit).
 *
 * xchg(addr, newval) atomically performs, as ONE indivisible step no other CPU or thread
 * can interleave with:
 *     old = *addr;  *addr = newval;  return old;
 *
 * This is xv6's primitive. In the real kernel (xv6 x86.h, line 121) it is inline assembly —
 * the x86 XCHG instruction with the LOCK behavior:
 *
 *     static inline uint xchg(volatile uint *addr, uint newval) {
 *       uint result;
 *       asm volatile("lock; xchgl %0, %1" :
 *                    "+m" (*addr), "=a" (result) :
 *                    "1" (newval) :
 *                    "cc");
 *       return result;
 *     }
 *
 * Here it is a portable function with the SAME signature and semantics: the compiler
 * intrinsic emits that very `xchg` instruction on x86-64 and the equivalent (SWPAL or an
 * LL/SC loop) on ARM64. The test harness instruments this choke point: it counts exchanges
 * and tracks which thread holds which lock word.
 *
 * Specification:
 *   addr    the lock word of a struct spinlock, i.e. &lk->locked, initialised by the provided
 *           init function. Nothing else: the harness registers every address exchanged here
 *           as a lock and tracks its holder.
 *   newval  the value to store. The harness reads 0 as a release of the lock word and any
 *           non-zero value as an acquire attempt.
 *   returns the value the word held immediately before the store.
 * It never blocks and never fails. (After every 64th exchange that finds the word non-zero
 * it lets another runnable thread have the CPU first — harmless on an idle core, and what
 * keeps a 2-core laptop usable when more threads spin than there are CPUs; the old value is
 * returned unchanged.)
 *
 * Aside (lecture 8): compare-and-swap is the stronger cousin — it only writes if the old
 * value matches an expected one. A spinlock needs no more than exchange.
 */
#ifndef COURSE_XV6_X86_H
#define COURSE_XV6_X86_H

#include "types.h"

uint xchg(volatile uint *addr, uint newval);

#endif
