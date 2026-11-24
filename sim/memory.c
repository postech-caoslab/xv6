/* memory.c -- physical memory: frames, frame_alloc(), kfree(). DO NOT MODIFY.
 * kalloc() (quiz/kalloc.c) gets its frames from frame_alloc(). */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "vm.h"
#include "sim.h"
#include "internal.h"

char        mem[NPAGE][PGSIZE];
uint       *pgdir = (uint*)mem[0];
struct sim  sim;

int  sim_fused[NPAGE];            /* frame in use? */
uint sim_owner[NPAGE];            /* page held by each frame (0: free or a page table) */

static char why[192];

void
sim_fail(const char *fmt, ...)
{
  va_list ap;

  if(sim.error)
    return;
  va_start(ap, fmt);
  vsnprintf(why, sizeof why, fmt, ap);
  va_end(ap);
  sim.error = why;
}

void
sim_logev(int type, uint va, uint pa)
{
  if(sim.nevent < MAXEVENT){
    sim.events[sim.nevent].type = type;
    sim.events[sim.nevent].va   = va;
    sim.events[sim.nevent].pa   = pa;
    sim.nevent++;
  }
}

/* frame number of a frame's physical address, or -1 (frame 0, the page directory: -1) */
int
sim_frameof(uint pa)
{
  if(pa % PGSIZE != 0 || pa < PGSIZE || pa >= (uint)sim.nframe * PGSIZE)
    return -1;
  return (int)(pa / PGSIZE);
}

/* the data stored in a page: depends on its address and how often it was written */
void
sim_fill(char *v, uint va, uint ver)
{
  uint *w = (uint*)v;
  int i;

  for(i = 0; i < PGSIZE / 4; i++)
    w[i] = (va | (uint)i) ^ (ver * 0x9E3779B9u);
}

int
sim_intact(char *v, uint va, uint ver)
{
  uint *w = (uint*)v;
  int i;

  for(i = 0; i < PGSIZE / 4; i++)
    if(w[i] != ((va | (uint)i) ^ (ver * 0x9E3779B9u)))
      return 0;
  return 1;
}

/* ---- frames */

/* a free frame: its physical address (frame 0 is the page directory, so 0 means full) */
uint
frame_alloc(void)
{
  int f;

  for(f = 1; f < sim.nframe; f++){
    if(!sim_fused[f]){
      sim_fused[f] = 1;
      memset(mem[f], 0, PGSIZE);       /* a new frame is all 0s */
      return (uint)f * PGSIZE;
    }
  }
  return 0;
}

void
kfree(uint pa)
{
  int f = sim_frameof(pa);

  if(f < 0){
    sim_fail("kfree: 0x%08X is not the address of a frame from kalloc()", pa);
    return;
  }
  if(!sim_fused[f]){
    sim_fail("kfree: frame %d is already free", f);
    return;
  }
  sim_fused[f] = 0;
  sim_owner[f] = 0;
  memset(mem[f], 1, PGSIZE);
}

int
frames_in_use(void)
{
  int f, n = 0;

  for(f = 1; f < sim.nframe; f++)
    n += sim_fused[f];
  return n;
}

int
page_tables(void)
{
  int i, n = 0;

  for(i = 0; i < NPDENTRIES; i++)
    if(pgdir[i] & PTE_P)
      n++;
  return n;
}
