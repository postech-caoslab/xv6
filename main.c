/* main.c -- loads a trace, runs scheduler(), checks the result. DO NOT MODIFY. */
#include <stdio.h>
#include <string.h>
#include "sched.h"

struct proc proc[NPROC];

struct pinfo {
  int state;
  int nice;
};

#ifdef BUILD_RR
/* Quiz 1: pids 1, 2, 0 (UNUSED), 3, 4; 20 ticks */
static const struct pinfo proc_info[] = {
  { RUNNABLE, 0 }, { RUNNABLE, 0 }, { UNUSED, 0 },
  { RUNNABLE, 0 }, { RUNNABLE, 0 },
};
static const int    policy  = SCHED_RR;
static const int    window  = 20;
static const int    points  = 10;
static const char  *title   = "Quiz 1: round robin";
static const int    order[] = { 1,2,3,4, 1,2,3,4, 1,2,3,4, 1,2,3,4, 1,2,3,4 };
static const int    norder  = 20;
static const int    picks   = 20;
static const int    slice   = 1;
static const int    pids[]  = { 1, 2, 3, 4 };
static const int    want[]  = { 5, 5, 5, 5 };
static const int    nwant   = 4;
#else
/* Quiz 2: pids 1, 2, 0 (UNUSED), 3 at nice -5, 0, -, 5; 480 ticks */
static const struct pinfo proc_info[] = {
  { RUNNABLE, -5 }, { RUNNABLE, 0 }, { UNUSED, 0 }, { RUNNABLE, 5 },
};
static const int    policy  = SCHED_CFS;
static const int    window  = 480;
static const int    points  = 30;
static const char  *title   = "Quiz 2: CFS";
static const int    order[] = { 1,2,3, 1,1,1,2, 1,1,1,2, 1,1,1,2, 3, 1,1,1,2 };
static const int    norder  = 20;
static const int    picks   = 240;
static const int    slice   = 2;
static const int    pids[]  = { 1, 2, 3 };
static const int    want[]  = { 334, 110, 36 };
static const int    nwant   = 3;
#endif

static const int num_proc = (int)(sizeof proc_info / sizeof proc_info[0]);

static int
cpu_ticks(int pid)
{
  int i, n = 0;

  for(i = 0; i < sim.nevent; i++)
    if(sim.events[i].pid == pid)
      n += sim.events[i].slice;
  return n;
}

static int
dispatches(int pid)
{
  int i, n = 0;

  for(i = 0; i < sim.nevent; i++)
    if(sim.events[i].pid == pid)
      n++;
  return n;
}

static void
print_order(int limit)
{
  int i;

  printf("  run order  :");
  for(i = 0; i < sim.nevent && i < limit; i++)
    printf(" %d", sim.events[i].pid);
  if(i < picks) printf(" ...");
  printf("\n");
}

/* 0 if the run matches the expected output, else the reason */
static const char *
check(char *msg, int len)
{
  int i, total = 0;

  if(sim.error) return sim.error;

  for(i = 0; i < sim.nevent; i++){
    if(sim.events[i].slice != slice){
      snprintf(msg, len, "a turn was granted %d ticks; expected %d",
               sim.events[i].slice, slice);
      return msg;
    }
    if(i < norder && sim.events[i].pid != order[i]){
      snprintf(msg, len, "dispatch %d went to pid %d; expected pid %d",
               i + 1, sim.events[i].pid, order[i]);
      return msg;
    }
  }
  if(sim.nevent != picks){
    snprintf(msg, len, "%d ticks in %d-tick turns is %d dispatches; got %d",
             window, slice, picks, sim.nevent);
    return msg;
  }
  for(i = 0; i < nwant; i++){
    int got = cpu_ticks(pids[i]);
    total += got;
    if(got != want[i]){
      snprintf(msg, len, "pid %d received %d ticks; expected %d",
               pids[i], got, want[i]);
      return msg;
    }
  }
  if(total != window){
    snprintf(msg, len, "only %d of the %d ticks were dispatched",
             total, window);
    return msg;
  }
  return 0;
}

int
main(void)
{
  static char msg[256];
  const char *why;
  struct proc *p;
  int i, nextpid = 1, nrunnable = 0;

  memset(&sim, 0, sizeof sim);
  memset(proc, 0, sizeof proc);
  ticks        = 0;
  sched_policy = policy;
  sim.window   = window;

  for(i = 0; i < num_proc; i++){
    p = &proc[i];
    p->nice  = proc_info[i].nice;
    p->state = proc_info[i].state;
    p->pid   = (p->state == UNUSED)? 0
                                   :nextpid++;
    p->vruntime = 0;
  }

  sim.nproc = num_proc;

  scheduler();

  for(i = 0; i < num_proc; i++)
    if(proc_info[i].state == RUNNABLE)
      nrunnable++;
  printf("%s  [%d points]\n", title, points);
  printf("  input      : %d processes, %d ticks\n", nrunnable, window);
  if(!sim.error)
    print_order(norder);

  why = check(msg, sizeof msg);
  if(why){
    printf("  RESULT: FAIL  0/%d points\n", points);
    printf("  %s\n", why);
    return 1;
  }
  printf("  ticks      :");
  for(i = 0; i < nwant; i++)
    printf(" pid%d %d", pids[i], cpu_ticks(pids[i]));
  printf("\n  dispatches :");
  for(i = 0; i < nwant; i++)
    printf(" pid%d %d", pids[i], dispatches(pids[i]));
  printf("\n  RESULT: PASS  %d/%d points\n", points, points);
  return 0;
}
