/* sched.h -- scheduler simulator types and interface. DO NOT MODIFY. */
#ifndef SCHED_H
#define SCHED_H

typedef unsigned int uint;

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct proc {
  int  pid;
  int  nice;                   /* -20 .. 19 */
  int  state;
  uint vruntime;
  int  ran;                    /* ticks run in the current slice */
  int  slice;                  /* ticks granted for the current slice */
};

#define NPROC 64

extern struct proc proc[NPROC];
extern uint ticks;

enum policy { SCHED_RR = 1, SCHED_CFS = 2 };
extern int sched_policy;

/* nice -> weight, indexed by nice + 20 */
static const unsigned int WEIGHT[40] __attribute__((unused)) = {
  /* -20 */ 88761, 71755, 56483, 46273, 36291,
  /* -15 */ 29154, 23254, 18705, 14949, 11916,
  /* -10 */  9548,  7620,  6100,  4904,  3906,
  /*  -5 */  3121,  2501,  1991,  1586,  1277,
  /*   0 */  1024,   820,   655,   526,   423,
  /*   5 */   335,   272,   215,   172,   137,
  /*  10 */   110,    87,    70,    56,    45,
  /*  15 */    36,    29,    23,    18,    15
};

#define sched_latency    6     /* ticks */
#define min_granularity  1     /* ticks */

/* Quiz (sched.c) */
struct proc* rr_pick(void);
struct proc* cfs_pick(void);
int  cfs_slice(void);
uint cfs_update_vrtime(int runtime, int nice, int vruntime);

/* Simulator (sched.c) */
int  sim_over(void);
void swtch(struct proc *p, int slice);
void scheduler(void);

/* Run log for main.c */
struct event { int tick, pid, slice; };

#define MAXEVENT 4096

struct sim {
  int          nproc;
  int          window;         /* ticks to simulate */
  struct event events[MAXEVENT];
  int          nevent;
  const char  *error;          /* set on an illegal pick or slice */
};
extern struct sim sim;

#endif
