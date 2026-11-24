/* swap.c -- Quiz 1: swap_out() and Quiz 3: swap_in().  kalloc() (Quiz 2) is in kalloc.c.
 * Types, macros and the provided functions are in include/vm.h. */
#include <stdio.h>
#include "vm.h"

/*
 * Swap out the oldest page (FIFO): move it to a swap slot and free its frame.
 *
 * Input : none (the page comes from the FIFO queue)
 * Output: the page's data in a slot, its PTE = the slot (PTE format: vm.h), its frame freed
 */
void
swap_out(void)
{
  uint *pte;        /* PTE of the page */
  int    slot;      /* the swap slot for the page */
  uint   pa;        /* the page's frame: its physical address */

  /* TODO: Q1
   

   */
}

/*
 * Swap in a page on a page fault: bring it back from its slot.
 *
 * Input : pte   the PTE of the page (swapped out: the slot number, present bit 0)
 * Output: the page's data in a frame, its PTE = the frame, the slot freed,
 *         the page at the head of the FIFO queue
 */
void
swap_in(uint *pte)
{
  int   slot;       /* the slot that holds the page */
  uint  pa;         /* the new frame for the page: its physical address */

  /* TODO: Q3
   

   */
}
