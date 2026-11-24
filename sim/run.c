/* run.c -- runs a trace: vmrun().  DO NOT MODIFY.
 * A trace is a list of   ALLOC va  (allocate a page at va),
 *                        READ va   (load from va),
 *                        WRITE va  (store to va).
 * As in a real system, the OS manages the page tables and the MMU uses them:
 *   ALLOC va         the OS: kalloc() a frame for the page (Quiz 2), find its PTE with
 *                    walkpgdir() (sim/walkpgdir.c; makes the page table if needed),
 *                    PTE = frame | PTE_P, enqueue()
 *   READ / WRITE va  the MMU (hardware): translates va by reading the PD entry and the
 *                    PTE straight from physical memory -- no walkpgdir() here
 *                    page fault -> the OS's fault handler:
 *                      no page table, or PTE is 0   -> segmentation fault
 *                      swapped out                  -> swap_in() (Quiz 3), then the MMU again
 * Tests 1 ... 3 set up their pages without a trace; test 4 runs a whole trace. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "vm.h"
#include "sim.h"
#include "internal.h"

uint sim_version[NVPAGE];          /* times each page was written */

/* ---- page tables, as the simulator sees them */

/* the PTE of va, or 0 if va has no page table */
uint*
sim_lookup(uint va)
{
  uint e = pgdir[PDX(va)];

  if(!(e & PTE_P) || (e >> 12) >= NPAGE)
    return 0;
  return (uint*)mem[e >> 12] + PTX(va);
}

/* sim_lookup() backwards: the va whose PTE is pte; 0 if pte is not in a page table */
int
sim_vaof(uint *pte, uint *va)
{
  uint i, j, e;

  for(i = 0; i < NPDENTRIES; i++){
    e = pgdir[i];
    if(!(e & PTE_P) || (e >> 12) >= NPAGE)
      continue;
    for(j = 0; j < NPTENTRIES; j++)
      if(pte == (uint*)mem[e >> 12] + j){
        *va = (i << 22) | (j << 12);
        return 1;
      }
  }
  return 0;
}

/* the frame at pa becomes the page table of va's 4MB region */
void
sim_settable(uint va, uint pa)
{
  pgdir[PDX(va)] = pa | PTE_P;
}

/* ---- the MMU (hardware): every READ / WRITE goes through it */

#define CR3 0x00000000                     /* PA of the page directory (frame 0) */

/* a word of physical memory: the MMU reads the tables by PA, not through pointers */
static uint
physread(uint pa)
{
  if(pa >= (uint)sim.nframe * PGSIZE){
    sim_fail("the MMU read PA 0x%08X, outside physical memory "
             "(does a PD entry hold a physical address?)", pa);
    return 0;
  }
  return *(uint*)(mem[0] + pa);
}

/* translate va as the MMU does on a load or store: from CR3, read the PD entry, then the
 * PTE, straight from physical memory.  0: *pa is the physical address.
 * -1: page fault (no page table, or the page is not present) */
static int
mmu_translate(uint va, uint *pa)
{
  uint pde, pte;

  pde = physread(CR3 + PDX(va) * 4);
  if(!(pde & PTE_P))
    return -1;
  pte = physread(PTE_ADDR(pde) + PTX(va) * 4);
  if(sim.error || !(pte & PTE_P))
    return -1;
  *pa = PTE_ADDR(pte) | PGOFF(va);
  return 0;
}

/* ---- the trace */

/* ALLOC va: (1) a frame for the page, (2) its PTE (the page table is made if needed),
 * (3) PTE = frame | PTE_P, (4) the page goes into the FIFO queue */
static void
run_alloc(uint va)
{
  uint *pte;
  uint   pa, oldpde;
  int    f;

  va = PGROUNDDOWN(va);
  if((pa = kalloc()) == 0){
    sim_fail("kalloc() returned 0 while allocating 0x%08X (does it swap a page out?)", va);
    return;
  }
  if(sim.error)
    return;
  if((f = sim_frameof(pa)) < 0 || !sim_fused[f]){
    sim_fail("kalloc() returned 0x%08X, which is not the address of a frame in use", pa);
    return;
  }
  oldpde = pgdir[PDX(va)];
  pte = walkpgdir(pgdir, va);
  if(sim.error)
    return;
  if(pte == 0 || pte != sim_lookup(va)){
    sim_fail("ALLOC 0x%08X: no page table for it (kalloc() failed?)", va);
    return;
  }
  if(pgdir[PDX(va)] != oldpde)              /* a new page table */
    sim_logev(EV_NEWPT, va, PTE_ADDR(pgdir[PDX(va)]));
  if(*pte != 0){
    sim_fail("ALLOC 0x%08X: the page is already allocated", va);
    return;
  }
  *pte = pa | PTE_P;
  sim_owner[f] = va;
  sim_version[va >> 12] = 0;
  sim_fill(mem[f], va, 0);
  sim_logev(EV_MAP, va, pa);
  enqueue(pte);
}

/* READ / WRITE va: the MMU translates va; on a page fault the OS handles it */
static void
run_read_write(const struct op *o)
{
  uint   va = o->va, page = PGROUNDDOWN(o->va), pa;
  uint *pte;
  int    f, slot;

  if(mmu_translate(va, &pa) < 0){           /* page fault: the OS's fault handler */
    if(sim.error)
      return;
    if(!(pgdir[PDX(va)] & PTE_P)){          /* no page table: segmentation fault */
      sim_logev(EV_FAULT, va, 0);
      return;
    }
    pte = walkpgdir(pgdir, va);             /* the page table exists: its PTE */
    if(*pte == 0){                          /* never allocated: segmentation fault */
      sim_logev(EV_FAULT, va, 1);
      return;
    }
    slot = (int)PTE_SLOT(*pte);             /* swapped out */
    swap_in(pte);
    if(sim.error)
      return;
    if(!(*pte & PTE_P)){
      sim_fail("after swap_in(), page 0x%08X does not have PTE_P", page);
      return;
    }
    if(sim_sused[slot]){
      sim_fail("swap_in() did not free slot %d of page 0x%08X", slot, page);
      return;
    }
    if(!sim_inqueue(pte)){
      sim_fail("after swap_in(), page 0x%08X is not in the FIFO queue", page);
      return;
    }
    if(mmu_translate(va, &pa) < 0){         /* the MMU tries again */
      if(!sim.error)
        sim_fail("after swap_in(), the MMU still cannot translate page 0x%08X", page);
      return;
    }
  }

  f = (int)(pa >> 12);
  if(f < 1 || f >= sim.nframe || !sim_fused[f]){
    sim_fail("page 0x%08X is mapped to frame %d, which is not in use", page, f);
    return;
  }
  if(!sim_intact(mem[f], page, sim_version[page >> 12])){
    sim_fail("page 0x%08X has the wrong data after coming back from swap", page);
    return;
  }
  sim_owner[f] = page;
  sim_logev(EV_READ, va, pa);
  if(o->type == WRITE){
    sim_version[page >> 12]++;
    sim_fill(mem[f], page, sim_version[page >> 12]);
  }
}

/* after every step: the PTEs, frames, slots and the FIFO queue must agree */
void
sim_check_swap(void)
{
  int i, j, f, s, inmem = 0, ondisk = 0, ntab = 0;

  for(i = 0; i < NPDENTRIES; i++){
    uint *pt;

    if(pgdir[i] == 0)
      continue;
    f = (int)(pgdir[i] >> 12);
    if(!(pgdir[i] & PTE_P) || f < 1 || f >= sim.nframe || !sim_fused[f]){
      sim_fail("PD[%d] = 0x%08X does not point to a page table in use", i, pgdir[i]);
      return;
    }
    ntab++;
    pt = (uint*)mem[f];
    for(j = 0; j < NPTENTRIES; j++){
      uint pte = pt[j];
      uint va  = ((uint)i << 22) | ((uint)j << 12);

      if(pte == 0)
        continue;
      if(pte & PTE_P){
        f = (int)(PTE_ADDR(pte) >> 12);
        if(f < 1 || f >= sim.nframe || !sim_fused[f]){
          sim_fail("page 0x%08X is mapped to frame %d, which is free (was its PTE updated?)", va, f);
          return;
        }
        inmem++;
      } else {                              /* swapped out */
        s = (int)PTE_SLOT(pte);
        if(PGOFF(pte) != 0){
          sim_fail("the swapped-out PTE of 0x%08X should be exactly slot << 12", va);
          return;
        }
        if(s < 1 || s >= NSLOT || !sim_sused[s]){
          sim_fail("page 0x%08X points to slot %d, which is not in use", va, s);
          return;
        }
        ondisk++;
      }
    }
  }
  if(frames_in_use() - ntab > inmem){
    sim_fail("%d frames hold pages but %d pages are in memory (missing kfree?)",
             frames_in_use() - ntab, inmem);
    return;
  }
  if(frames_in_use() - ntab < inmem){
    sim_fail("%d frames hold pages but %d PTEs have PTE_P (was PTE_P cleared in swap_out?)",
             frames_in_use() - ntab, inmem);
    return;
  }
  if(slots_in_use() != ondisk){
    sim_fail("%d slots are in use but %d pages are swapped out (missing slotfree?)",
             slots_in_use(), ondisk);
    return;
  }
  if(fifo_count != inmem){
    sim_fail("%d pages are in memory but %d are in the FIFO queue", inmem, fifo_count);
    return;
  }
}

void
sim_reset_state(void)
{
  memset(mem, 0, sizeof mem);           /* frame 0: an empty page directory */
  memset(sim_fused, 0, sizeof sim_fused);
  memset(sim_sused, 0, sizeof sim_sused);
  memset(disk, 0xEE, sizeof disk);
  memset(sim_owner, 0, sizeof sim_owner);
  memset(sim_sowner, 0, sizeof sim_sowner);
  memset(sim_version, 0, sizeof sim_version);
  sim_fused[0] = 1;
  fifo_tail = fifo_count = 0;
}

/* runs sim.ops[] from the state left by sim_reset() */
void
vmrun(void)
{
  int i;

  for(i = 0; i < sim.nop && !sim.error; i++){
    const struct op *o = &sim.ops[i];

    if(o->type == ALLOC)
      run_alloc(o->va);
    else                                    /* READ, WRITE */
      run_read_write(o);
    sim_check_swap();
  }
}
