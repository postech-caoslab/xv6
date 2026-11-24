/* walkpgdir.c -- walkpgdir(): the OS finds the PTE of a virtual address. DO NOT MODIFY.
 * ALLOC (sim/run.c) uses it to map a new page, and the page fault handler uses it
 * to find the PTE of the page that faulted.
 * Types, macros and functions are in include/vm.h. */
#include "vm.h"

/*
 * Find the PTE of va in the two-level page table.
 * If va has no page table yet, make one.
 *
 * Input : pgdir   the page directory
 *         va      a virtual address
 * Return: a pointer to the PTE of va (0 if memory is full)
 */
uint*
walkpgdir(uint *pgdir, uint va)
{
  uint *pde;        /* pointer to the PD entry of va */
  uint *pgtab;      /* pointer to the page table of va; pgtab[i]: PTE i */
  uint  pa;         /* a new frame for the page table */

  pde = &pgdir[PDX(va)];                  /* the PD entry of va */
  if(!(*pde & PTE_P)){                    /* no page table yet: make one */
    if((pa = kalloc()) == 0)              /* memory is full */
      return 0;
    *pde = pa | PTE_P;                    /* a PD entry holds a physical address */
  }
  pgtab = (uint*)mem[PTE_ADDR(*pde) >> 12];  /* the page table of va: frame PTE_ADDR(*pde) >> 12 */
  return &pgtab[PTX(va)];                 /* the PTE of va */
}
