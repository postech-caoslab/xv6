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
 * LL/SC loop) on ARM64. The test harness instruments this choke point: it counts your spin
 * attempts and tracks which thread holds which lock.
 *
 * Specification:
 *   addr    the lock word of a struct spinlock, i.e. &lk->locked, initialised by the provided
 *           init function. Nothing else: the harness registers every address exchanged here
 *           as a lock and tracks its holder.
 *   newval  the value to store. Any uint is exchanged; the lock protocol uses exactly two:
 *           1 = "I am taking the lock", 0 = "I am handing it back". The harness reads 0 as
 *           a release and any non-zero value as an acquire attempt.
 *   returns the value the word held immediately before the store. For newval 1: 0 means
 *           your exchange took the lock (you are the holder from this instant), non-zero
 *           means someone else holds it and nothing changed — try again.
 * It never blocks, never fails, and never spins: the loop is yours to write. (After every
 * 64th failed attempt it lets another runnable thread have the CPU first — harmless on
 * an idle core, and what keeps a 2-core laptop usable when more threads spin than there
 * are CPUs; the old value is returned unchanged.)
 *
 * Aside (lecture 8): compare-and-swap is the stronger cousin — it only writes if the old
 * value matches an expected one. A spinlock needs no more than exchange.
 */
#ifndef COURSE_XV6_X86_H
#define COURSE_XV6_X86_H

#include "types.h"

uint xchg(volatile uint *addr, uint newval);

#endif
