/* fifo.c -- the FIFO queue: enqueue(), dequeue(). DO NOT MODIFY.
 *
 *   head (newest)  page 4 -> page 3 -> page 2 -> page 1  tail (oldest)
 *
 * enqueue() puts a page at the head; dequeue() takes the page at the
 * tail, the one that came into memory first (FIFO). */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "vm.h"
#include "sim.h"
#include "internal.h"

uint *fifoqueue[NPTENTRIES];   /* circular buffer of PTEs: fifoqueue[fifo_tail] is the oldest */
int fifo_tail, fifo_count;

/* ---- the FIFO queue */

int
sim_inqueue(uint *pte)
{
  int i;

  for(i = 0; i < fifo_count; i++)
    if(fifoqueue[(fifo_tail + i) % NPTENTRIES] == pte)
      return 1;
  return 0;
}

/* put the page at the head (the newest end) */
void
enqueue(uint *pte)
{
  int  head;
  uint va;

  if(!sim_vaof(pte, &va)){
    sim_fail("enqueue: not the PTE of a page");
    return;
  }
  if(sim_inqueue(pte)){
    sim_fail("enqueue: page 0x%08X is already in the FIFO queue", va);
    return;
  }
  head = (fifo_tail + fifo_count) % NPTENTRIES;
  fifoqueue[head] = pte;
  fifo_count++;
}

/* take the page at the tail (the oldest): its PTE */
uint*
dequeue(void)
{
  uint *pte, va = 0;

  if(fifo_count == 0)
    return 0;
  pte = fifoqueue[fifo_tail];
  fifo_tail = (fifo_tail + 1) % NPTENTRIES;
  fifo_count--;
  if(!(*pte & PTE_P)){
    sim_vaof(pte, &va);
    sim_fail("dequeue: page 0x%08X in the queue is not in memory", va);
  }
  return pte;
}
