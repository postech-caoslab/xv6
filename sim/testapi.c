/* testapi.c -- simulator setup and inspection used by the tests. DO NOT MODIFY.
 * The setup functions use frame_alloc() directly, never the quiz code. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "vm.h"
#include "sim.h"
#include "internal.h"

void
sim_reset(int nframe)
{
  memset(&sim, 0, sizeof sim);
  sim.nframe = nframe;
  sim_reset_state();
}

/* the PTE of va; makes its page table if needed */
static uint*
setup_pte(uint va)
{
  uint pa;

  if(sim_lookup(va) == 0){
    if((pa = frame_alloc()) == 0){
      sim_fail("test setup: out of memory");
      return 0;
    }
    sim_settable(va, pa);
  }
  return sim_lookup(va);
}

/* allocate page va without the quiz code, like ALLOC: a frame for the page,
 * the page table if needed, the PTE, the FIFO queue */
void
sim_map(uint va)
{
  uint *pte;
  uint   pa;
  int    f;

  va = PGROUNDDOWN(va);
  if((pa = frame_alloc()) == 0){
    sim_fail("test setup: out of memory");
    return;
  }
  if((pte = setup_pte(va)) == 0)
    return;
  f = sim_frameof(pa);
  *pte = pa | PTE_P;
  sim_version[va >> 12] = 0;
  sim_owner[f] = va;
  sim_fill(mem[f], va, 0);
  enqueue(pte);
}

/* page va starts out in a swap slot */
void
sim_put_in_swap(uint va)
{
  uint *pte;
  int    s;

  va = PGROUNDDOWN(va);
  if((pte = setup_pte(va)) == 0 || (s = slotalloc()) < 0)
    return;
  sim_version[va >> 12] = 0;
  sim_fill(disk[s], va, 0);
  sim_sowner[s] = va;
  *pte = (uint)s << 12;
}

/* the PTE of page va (its page table must exist) */
uint*
sim_pte(uint va)
{
  static uint none;
  uint *pte = sim_lookup(va);

  return pte? pte : &none;
}

/* frame number if pa is the address of a frame in use, else -1 */
int
sim_frame(uint pa)
{
  int f = sim_frameof(pa);

  return (f >= 0 && sim_fused[f])? f : -1;
}

int
sim_frame_used(int f)
{
  return f >= 1 && f < sim.nframe && sim_fused[f];
}

int
sim_slot_used(int s)
{
  return s >= 1 && s < NSLOT && sim_sused[s];
}

/* frames that hold pages (page tables do not count) */
int
pages_in_memory(void)
{
  return frames_in_use() - page_tables();
}

/* page va is in memory and holds its own data */
int
sim_has_data(uint va)
{
  uint *pte = sim_lookup(va);
  int    f;

  if(pte == 0 || !(*pte & PTE_P))
    return 0;
  f = (int)(PTE_ADDR(*pte) >> 12);
  return sim_frame_used(f) && sim_intact(mem[f], PGROUNDDOWN(va), sim_version[va >> 12]);
}

/* slot s holds the data of page va */
int
sim_slot_has(int s, uint va)
{
  return sim_slot_used(s) && sim_intact(disk[s], PGROUNDDOWN(va), sim_version[va >> 12]);
}

/* the FIFO queue, oldest (tail) first: the pages' VAs; returns its length */
int
sim_queue(uint *va, int max)
{
  int i;

  for(i = 0; i < fifo_count && i < max; i++){
    va[i] = 1;                              /* no page has this va (a PTE in no page table) */
    sim_vaof(fifoqueue[(fifo_tail + i) % NPTENTRIES], &va[i]);
  }
  return fifo_count;
}

/* 0 if the PTEs, frames, slots and queue agree, else the reason */
const char*
sim_check(void)
{
  sim_check_swap();
  return sim.error;
}
