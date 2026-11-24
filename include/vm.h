/* vm.h -- everything the quiz code uses: types, macros, functions. DO NOT MODIFY. */
#ifndef VM_H
#define VM_H

typedef unsigned int uint;

/* 32-bit virtual address:  | PDX (10 bits) | PTX (10 bits) | offset (12 bits) | */
#define PGSIZE          4096
#define NPDENTRIES      1024
#define NPTENTRIES      1024
#define PDX(va)         (((uint)(va) >> 22) & 0x3FF)    /* page directory index */
#define PTX(va)         (((uint)(va) >> 12) & 0x3FF)    /* page table index */
#define PGOFF(va)       ((uint)(va) & 0xFFF)
#define PGROUNDDOWN(a)  ((uint)(a) & ~(PGSIZE - 1))

/* A PD entry is   page table address | PTE_P   (or 0: no page table yet).
 * A PTE is in one of three states:
 *   frame address | PTE_P     page in memory
 *   slot << 12   (PTE_P = 0)  page swapped out to that slot (slot >= 1)
 *   0                         never allocated
 * PTE_P is the same bit as in x86 and xv6.  When it is 0 the hardware
 * ignores the other bits, so the OS can keep the slot number there. */
#define PTE_P           0x001   /* present */

#define PTE_ADDR(e)     ((uint)(e) & ~0xFFF)    /* the physical address in an entry (flags cleared) */
#define PTE_SLOT(e)   ((uint)(e) >> 12)     /* the slot number in a swapped-out PTE */

/* Physical memory: frame f is at physical address f * PGSIZE.
 * The quiz code handles a frame by its physical address (PA): kalloc() returns one,
 * a PTE holds one (PTE_ADDR), kfree(), swapwrite() and swapread() take one.
 * Frame 0 is the page directory.  Page tables and pages use the other frames.
 * Slot 0 is never handed out by slotalloc(), so a swapped-out PTE is never 0. */
#define NPAGE  64                   /* frames of physical memory */
#define NSLOT  64

extern char   mem[NPAGE][PGSIZE];      /* frame f is mem[f] */
extern uint *pgdir;

/* quiz/ */
void   swap_out(void);                   /* Quiz 1  swap.c: you write it          */
uint   kalloc(void);                     /* Quiz 2  kalloc.c: given; you change it */
void   swap_in(uint *pte);               /* Quiz 3  swap.c: you write it          */

/* Provided by the simulator (sim/) */
uint*  walkpgdir(uint *pgdir, uint va);  /* the PTE of va; makes its page table if needed */
uint   frame_alloc(void);                /* a free frame filled with 0s: its PA, or 0 if memory is full */
void   kfree(uint pa);                   /* give the frame at pa back */
int    slotalloc(void);                  /* a free swap slot (never 0) */
void   slotfree(int slot);               /* give a slot back */
void   swapwrite(uint pa, int slot);     /* the frame at pa -> slot (4KB) */
void   swapread(uint pa, int slot);      /* slot -> the frame at pa (4KB) */
void   enqueue(uint *pte);               /* put the page at the head of the FIFO queue */
uint*  dequeue(void);                    /* take the page at the tail (the oldest): its PTE */

#endif
