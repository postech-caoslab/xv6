/* internal.h -- state shared by the simulator files in sim/. DO NOT MODIFY. */
#ifndef SIM_INTERNAL_H
#define SIM_INTERNAL_H

#define NVPAGE (1u << 20)                   /* virtual pages: va >> 12 */

/* memory.c */
extern int  sim_fused[NPAGE];              /* frame in use? */
extern uint sim_owner[NPAGE];              /* page held by each frame (0: free or a page table) */
void sim_fail(const char *fmt, ...);        /* record an illegal result in sim.error */
void sim_logev(int type, uint va, uint pa); /* append to sim.events[] */
int  sim_frameof(uint pa);                  /* frame number of a frame's physical address, or -1 */
void sim_fill(char *v, uint va, uint ver);  /* write the data of page va */
int  sim_intact(char *v, uint va, uint ver);

/* disk.c */
extern int  sim_sused[NSLOT];               /* slot in use? */
extern char disk[NSLOT][PGSIZE];        /* swap area */
extern uint sim_sowner[NSLOT];              /* page held by each slot */

/* fifo.c */
extern uint *fifoqueue[NPTENTRIES];        /* the FIFO queue: the pages' PTEs */
extern int  fifo_tail, fifo_count;          /* oldest entry, number of entries */
int  sim_inqueue(uint *pte);

/* run.c */
extern uint sim_version[NVPAGE];            /* times each page was written */
uint* sim_lookup(uint va);                  /* the PTE of va, or 0 if it has no page table */
int   sim_vaof(uint *pte, uint *va);        /* sim_lookup() backwards: the va of a PTE; 0 if none */
void  sim_settable(uint va, uint pa);       /* the frame at pa becomes the page table of va */
void  sim_check_swap(void);
void  sim_reset_state(void);

#endif
