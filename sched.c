/* sched.c -- scheduler simulator. Fill in the TODOs. */
#include <stdio.h>
#include "sched.h"

uint ticks;
int  sched_policy = SCHED_RR;

/* ---------------------------------------------------------------- Quiz 1 */

struct proc*
rr_pick(void)
{
  /* TODO: Q1*/
  return 0;
}

/* ---------------------------------------------------------------- Quiz 2 */

struct proc*
cfs_pick(void)
{
  /* TODO: Q2*/
  return 0;
}

int
cfs_slice(void)
{
  /* TODO: Q3 */
  return 0;
}

uint
cfs_update_vrtime(int runtime, int nice, int vruntime)
{
  /* TODO: Q4 */
  return 0;
}

/* ------------------------------------------------ Simulator: DO NOT MODIFY */

struct sim sim;

int
sim_over(void)
{
  return sim.error != 0 || (int)ticks >= sim.window;
}

void
swtch(struct proc *p, int slice)
{
  static char why[128];

  /* reject an illegal pick or slice; this ends the run */
  if(p == 0){
    sim.error = "the pick returned 0 (no process)";
    return;
  }
  if(p < proc || p >= &proc[sim.nproc]){
    sim.error = "the pick is not a slot in proc[]";
    return;
  }
  if(p->state != RUNNABLE){
    snprintf(why, sizeof why, "the pick is pid %d, which is not RUNNABLE",
             p->pid);
    sim.error = why;
    return;
  }
  if(slice < 1){
    snprintf(why, sizeof why, "time slice %d is less than 1 tick", slice);
    sim.error = why;
    return;
  }
  if(sim.nevent < MAXEVENT){
    sim.events[sim.nevent].tick  = (int)ticks;
    sim.events[sim.nevent].pid   = p->pid;
    sim.events[sim.nevent].slice = slice;
    sim.nevent++;
  }

  p->state = RUNNING;
  p->ran   = 0;
  p->slice = slice;

  for(;;){         /* one iteration = one tick */

    ticks++;
    p->ran++;

    if(sched_policy == SCHED_CFS)
      p->vruntime = cfs_update_vrtime(1, p->nice,
                                      p->vruntime); /* Q2 */

    if(p->ran >= p->slice){  /* yield: back to RUNNABLE */
      p->state = RUNNABLE;
      return;
    }
  }
}

void
scheduler(void)
{
  struct proc *p = 0;
  int time_slice = 0;

  while(!sim_over()){
    switch(sched_policy){

    case SCHED_RR:
      p = rr_pick(); /* Q1 */
      time_slice = 1;
      break;

    case SCHED_CFS:
      p = cfs_pick(); /* Q2 */
      time_slice = cfs_slice(); /* Q2 */
      break;
    }

    swtch(p, time_slice);
  }
}
