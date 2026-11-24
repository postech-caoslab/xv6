/* sim.h -- the simulator interface for the tests (test/). DO NOT MODIFY. */
#ifndef SIM_H
#define SIM_H

#include "vm.h"

/* Trace and run log, read by the tests */
enum optype { ALLOC = 1, READ, WRITE };   /* allocate a page, load, store */
struct op { int type; uint va; };

/* EV_NEWPT  ALLOC made a page table       pa = its physical address
 * EV_MAP    ALLOC gave the page a frame    pa = the frame's physical address
 * EV_READ   READ / WRITE used the page     pa = the physical address
 * EV_FAULT  segmentation fault             pa = 1 if the page table exists (the PTE is 0)
 * EV_OUT / EV_IN  swap I/O                 va = page, pa = slot */
enum evtype { EV_READ = 1, EV_FAULT, EV_OUT, EV_IN, EV_MAP, EV_NEWPT };
struct event { int type; uint va, pa; };

#define MAXEVENT 4096

struct sim {
  int              nframe;     /* frames in memory, including the page directory */
  const struct op *ops;
  int              nop;
  struct event     events[MAXEVENT];
  int              nevent;
  int              nout, nin;  /* swap I/O */
  const char      *error;      /* set on an illegal result */
};
extern struct sim sim;

int frames_in_use(void);
int slots_in_use(void);
int page_tables(void);
int pages_in_memory(void);                /* frames in use that hold pages, not page tables */

/* Test support (sim/testapi.c) */
void        sim_reset(int nframe);      /* empty memory, no slots used, empty queue */
void        sim_map(uint va);           /* allocate page va without the quiz code */
void        sim_put_in_swap(uint va);   /* page va starts in a swap slot */
uint*       sim_pte(uint va);           /* the PTE of page va */
int         sim_frame(uint pa);         /* frame number if pa is a frame in use, else -1 */
int         sim_frame_used(int f);
int         sim_slot_used(int s);
int         sim_has_data(uint va);      /* page va is in memory with its data */
int         sim_slot_has(int s, uint va);  /* slot s holds page va's data */
int         sim_queue(uint *va, int max);  /* the FIFO queue, oldest (tail) first */
const char* sim_check(void);            /* 0 if PTEs, frames, slots, queue agree */

void vmrun(void);                       /* runs sim.ops[] (sim/run.c) */

#endif
