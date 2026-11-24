/* disk.c -- swap area: slotalloc(), slotfree(), swapwrite(), swapread(). DO NOT MODIFY. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "vm.h"
#include "sim.h"
#include "internal.h"

int  sim_sused[NSLOT];             /* slot in use? */
char disk[NSLOT][PGSIZE];      /* swap area */
uint sim_sowner[NSLOT];            /* page held by each slot */

/* ---- swap slots */

int
slotalloc(void)
{
  int s;

  for(s = 1; s < NSLOT; s++){          /* slot 0 is never used */
    if(!sim_sused[s]){
      sim_sused[s] = 1;
      return s;
    }
  }
  sim_fail("slotalloc: no free swap slot (are slots freed in swap_in?)");
  return -1;
}

void
slotfree(int slot)
{
  if(slot < 1 || slot >= NSLOT || !sim_sused[slot]){
    sim_fail("slotfree: slot %d is not in use", slot);
    return;
  }
  sim_sused[slot] = 0;
}

int
slots_in_use(void)
{
  int s, n = 0;

  for(s = 1; s < NSLOT; s++)
    n += sim_sused[s];
  return n;
}

void
swapwrite(uint pa, int slot)
{
  int f = sim_frameof(pa);

  if(f < 0 || !sim_fused[f]){
    sim_fail("swapwrite: 0x%08X is not the address of a frame in use", pa);
    return;
  }
  if(slot < 1 || slot >= NSLOT || !sim_sused[slot]){
    sim_fail("swapwrite: slot %d was not allocated with slotalloc()", slot);
    return;
  }
  memcpy(disk[slot], mem[f], PGSIZE);
  sim_sowner[slot] = sim_owner[f];
  sim.nout++;
  sim_logev(EV_OUT, sim_owner[f], (uint)slot);
}

void
swapread(uint pa, int slot)
{
  int f = sim_frameof(pa);

  if(f < 0 || !sim_fused[f]){
    sim_fail("swapread: 0x%08X is not the address of a frame in use", pa);
    return;
  }
  if(slot < 1 || slot >= NSLOT || !sim_sused[slot]){
    sim_fail("swapread: slot %d is not in use", slot);
    return;
  }
  memcpy(mem[f], disk[slot], PGSIZE);
  sim_owner[f] = sim_sowner[slot];
  sim.nin++;
  sim_logev(EV_IN, sim_sowner[slot], (uint)slot);
}
