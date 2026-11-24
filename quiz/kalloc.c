/* kalloc.c -- kalloc(): allocate a frame of physical memory.
 * walkpgdir() and the simulator (ALLOC) call it.
 * Quiz 2: change it so that it swaps a page out when memory is full.
 * Types, macros and the provided functions are in include/vm.h. */
#include <stdio.h>
#include "vm.h"

/*
 * Allocate one frame (4096 bytes, filled with 0s).
 *
 * Input : none
 * Return: the physical address of the frame
 */
uint
kalloc(void)
{
  uint pa;          /* the frame to return: its physical address */

  pa = frame_alloc();

  /* TODO: Q2
   
   */

  return pa;
}
