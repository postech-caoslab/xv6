// statquiz2 -- the two-sided check on picked, notpicked and wait.
//
// GIVEN. Do not modify.
//
// Comparing the kernel with what a process could see of itself is necessarily
// one-sided (kernel >= probe): a process cannot observe choices it lost while
// it was off the CPU. So a kernel that counts notpicked in the wrong place, or
// wait at the wrong moment, can still report numbers large enough to pass such
// a comparison.
//
// This program removes the guesswork. Two phases:
//
//   pair   exactly TWO CPU-bound children run while everything else in the
//          system sleeps, so the counters of one child are pinned by the
//          counters of the other, with no probe involved:
//
//            wait_A      == run_B      whenever B held the CPU, A was ready
//                                      and waiting
//            notpicked_A == picked_B   every turn given to B is one choice A
//                                      lost, and A loses no choice while A
//                                      itself is picked
//            the parent's counters stay put -- it slept on a pipe the whole
//            time, so it was never RUNNABLE: it lost nothing and waited for
//            nothing
//
//          run_A and run_B are read from getrunticks() inside each child and
//          sent to the parent, so no counter under test appears on both sides
//          of a check. Each child also snapshots its own counters at both ends
//          of the window and sends the differences, so the comparison is
//          window against window.
//
//          Two more come from CFS itself, with the children at nice 0 and 5:
//
//            A gets about 3x B's CPU     weights 1024 : 335
//            turns are about ticks / 8   the slice is CFS_LATENCY / 2 with two
//                                        runnable, and CFS_LATENCY is 16
//
//   kill   a sleeper is killed while a CPU-bound sibling keeps the CPU busy.
//          kill() on a SLEEPING process is one of the paths that writes
//          RUNNABLE, so it must record the tick like the others:
//
//            wait small     it slept, it did not wait; only the ticks from
//                           kill() to its last turn count
//            picked == 2    once to reach the pipe, once to die. A counter
//                           incremented at yield() gives 0 here: the sleeper
//                           never yielded, it slept and exited
//
// What each check catches, verified by breaking the reference on purpose:
//   notpicked counted at a state change (yield)  -> notpicked_B tracks
//                                                   picked_B, not picked_A
//   notpicked also counted for sleepers          -> the parent's grows
//   wait added at yield instead of at the pick   -> wait_B tracks run_B
//   a missed RUNNABLE stamp (yield, wakeup)      -> wait far above the other
//                                                   child's run
//   a missed RUNNABLE stamp (kill)               -> the killed sleeper's wait
//                                                   is its sleep
//   picked counted at yield, not at the pick     -> the sleeper has picked 0
//
// Usage: statquiz2

#include "types.h"
#include "stat.h"
#include "user.h"

// ---- check-row printer ------------------------------------------------
//
// One line per check: what was checked, what this run measured, what the
// check required, and ok or FAIL. A failure adds a line saying why.
//
// xv6's printf understands %d %x %p %s %c and nothing else -- no field
// widths -- so the columns are padded by hand.

// The shape of a requirement, for the `want` column.
#define W_NEAR   0         // a +-b
#define W_MIN    1         // >=a
#define W_MAX    2         // <=a
#define W_RANGE  3         // a..b
#define W_POS    4         // > 0
#define W_FLAG   5         // a is already the verdict; nothing to measure

#define R_WHAT  38         // column widths
#define R_GOT    8
#define R_WANT  15

static int __attribute__((unused))
r_absdiff(int a, int b)
{
  return a > b ? a - b : b - a;
}

static int __attribute__((unused))
r_numlen(int v)
{
  int n = 1, t = v;

  if(t < 0){ n++; t = -t; }
  while(t >= 10){ t /= 10; n++; }
  return n;
}

static void __attribute__((unused))
r_pad(char *s, int w)
{
  int i, n = 0;

  while(s[n])
    n++;
  printf(1, "%s", s);
  for(i = n; i < w; i++)
    printf(1, " ");
}

static void __attribute__((unused))
r_padnum(int v, int w)
{
  int i;

  for(i = r_numlen(v); i < w; i++)
    printf(1, " ");
  printf(1, "%d", v);
}

static void __attribute__((unused))
r_header(void)
{
  printf(1, "  ");
  r_pad("check", R_WHAT);
  printf(1, "     got  ");
  r_pad("want", R_WANT);
  printf(1, "  result\n");
}

// One check. `kind` says how a and b are read; `why` is printed only on a
// failure, and `counter` is bumped only on a failure.
static void __attribute__((unused))
r_check(int *counter, char *what, int got, int kind, int a, int b, char *why)
{
  int ok, n = 0;

  switch(kind){
  case W_NEAR:  ok = r_absdiff(got, a) <= b; break;
  case W_MIN:   ok = got >= a; break;
  case W_MAX:   ok = got <= a; break;
  case W_RANGE: ok = got >= a && got <= b; break;
  case W_POS:   ok = got > 0; break;
  default:      ok = a; break;
  }

  printf(1, "  ");
  r_pad(what, R_WHAT);
  if(kind == W_FLAG)
    r_pad("       -", R_GOT);
  else
    r_padnum(got, R_GOT);
  printf(1, "  ");

  switch(kind){
  case W_NEAR:  printf(1, "%d +-%d", a, b); n = r_numlen(a) + 3 + r_numlen(b); break;
  case W_MIN:   printf(1, ">=%d", a);       n = 2 + r_numlen(a); break;
  case W_MAX:   printf(1, "<=%d", a);       n = 2 + r_numlen(a); break;
  case W_RANGE: printf(1, "%d..%d", a, b);  n = r_numlen(a) + 2 + r_numlen(b); break;
  case W_POS:   printf(1, "> 0");           n = 3; break;
  default:      printf(1, "as stated");     n = 9; break;
  }
  while(n++ < R_WANT)
    printf(1, " ");

  if(ok){
    printf(1, "  ok\n");
  } else {
    printf(1, "  FAIL\n");
    (*counter)++;
    printf(1, "     -> %s\n", why);
  }
}

// A line that is printed but not graded: it corroborates a graded check rather
// than adding a second penalty for the same mistake.
static void __attribute__((unused))
r_info(char *what, int got, int want)
{
  int n;

  printf(1, "  ");
  r_pad(what, R_WHAT);
  r_padnum(got, R_GOT);
  printf(1, "  ");
  printf(1, "~%d", want);
  n = 1 + r_numlen(want);
  while(n++ < R_WANT)
    printf(1, " ");
  printf(1, "  info\n");
}

// A bug that the handout does not ask about: printed, never counted.
static void __attribute__((unused))
r_note(char *what, char *why)
{
  printf(1, "  note: %s: %s\n", what, why);
}

// ---- end of check-row printer ------------------------------------------

#define NSTAT   4          // buf[0..3]: vruntime, picked, notpicked, wait

static int failures;
static volatile int sink;

static void
fail(char *phase, char *why)
{
  failures++;
  printf(2, "statquiz2: FAIL: %s: %s\n", phase, why);
}

// One short unit of work.
static void
unit(void)
{
  int i, s = 0;

  for(i = 0; i < 500; i++)
    s += i ^ (s >> 3);
  sink = s;
}

// Zero the buffer before every call, so a slot the kernel does not write yet
// reads as 0 rather than as whatever was on the stack. That is what lets this
// program report "not implemented" instead of nonsense.
static int
sample(int pid, int *buf)
{
  int i;

  for(i = 0; i < NSTAT; i++)
    buf[i] = 0;
  return procstat(pid, buf);
}

// What a child reports about the window, and only about the window. The
// counters in buf are lifetime totals, so the child snapshots its own at both
// ends of the window and sends the differences. Comparing window against
// window is what makes wait_A == run_B exact rather than approximate: the
// ticks before the gate, and the turns the child takes after it reports, are
// outside both sides of the comparison.
struct creport {
  int run;                       // getrunticks() over the window
  int d[NSTAT];                  // procstat deltas over the window
};

static void
child(int nice, int gatefd, int donefd, int holdfd)
{
  int gate[2], r0, i;
  int s0[NSTAT], s1[NSTAT];
  struct creport rep;
  char c;

  setnice(0, nice);
  if(read(gatefd, gate, sizeof(gate)) != sizeof(gate)) exit();
  while(uptime() < gate[0]) unit();
  r0 = getrunticks();
  if(sample(0, s0) < 0) exit();

  while(uptime() < gate[1]) unit();
  rep.run = getrunticks() - r0;
  if(sample(0, s1) < 0) exit();

  for(i = 0; i < NSTAT; i++)
    rep.d[i] = s1[i] - s0[i];
  write(donefd, &rep, sizeof(rep));
  read(holdfd, &c, 1);
  exit();
}

static void
show(char *who, struct creport *r)
{
  printf(1, "  %s: over the window picked %d notpicked %d wait %d,"
            " run %d ticks\n",
         who, r->d[1], r->d[2], r->d[3], r->run);
}

// The bounds below are derived, not padding. Ticks are deterministic here --
// the whole trace reproduces byte for byte across runs -- so the only slack a
// correct kernel needs is at the edges of the window:
//
//   wait is credited when a process is PICKED, so the interval that is still
//   open when the window closes is credited outside it. That costs the child
//   which is waiting at the boundary up to one slice of wait, and nothing
//   else: measured at 4 ticks for both a 240-tick and a 480-tick window, on
//   whichever child happened to be waiting at the end. The slice here is
//   CFS_LATENCY / 2 = 8, because two processes are runnable -- not
//   CFS_LATENCY, which is the solo slice the Q1 phase runs under. Hence the
//   band is 8 + 2 below the other child's run ticks, and only 2 above: a
//   missed RUNNABLE stamp inflates wait, so the upper edge is the tight one.
//
//   a pick can likewise land just outside the window, which is worth +-3.
#define CFS_LATENCY   16            // the take-home fixes this at 16 ticks
#define PAIR_SLICE    (CFS_LATENCY / 2)  // two runnable: 16 / 2 = 8 ticks
#define WINDOW_SLACK  2        // the window's own ticks must all be accounted
#define WAIT_UNDER    (PAIR_SLICE + 2)
#define WAIT_OVER     2
#define PICK_SLACK    3
#define PARENT_SLACK  2

static void
pair_phase(int niceA, int niceB, int window)
{
  char *name = "Q2 pair";
  int gp[2], dpA[2], dpB[2], hp[2], gate[2];
  int pidA, pidB, A[NSTAT], B[NSTAT], me0[NSTAT], me1[NSTAT];
  struct creport ra, rb;
  int slices, spread;

  printf(1, "statquiz2: ---- two children pin each other ----\n");

  if(pipe(gp) < 0 || pipe(dpA) < 0 || pipe(dpB) < 0 || pipe(hp) < 0){
    fail(name, "pipe failed");
    return;
  }
  if(sample(0, me0) < 0){
    fail(name, "procstat(0, buf) failed");
    return;
  }

  pidA = fork();
  if(pidA < 0){ fail(name, "fork failed"); return; }
  if(pidA == 0){
    close(gp[1]); close(dpA[0]); close(dpB[0]); close(dpB[1]); close(hp[1]);
    child(niceA, gp[0], dpA[1], hp[0]);
  }
  pidB = fork();
  if(pidB < 0){ fail(name, "fork failed"); return; }
  if(pidB == 0){
    close(gp[1]); close(dpA[0]); close(dpA[1]); close(dpB[0]); close(hp[1]);
    child(niceB, gp[0], dpB[1], hp[0]);
  }
  close(gp[0]); close(dpA[1]); close(dpB[1]); close(hp[0]);

  // Open the window a few ticks out so both children are already spinning at
  // the gate when it starts, then block on the done pipes: from here until
  // both children have finished, the only runnable processes are A and B.
  gate[0] = uptime() + 4;
  gate[1] = gate[0] + window;
  write(gp[1], gate, sizeof(gate));
  write(gp[1], gate, sizeof(gate));
  close(gp[1]);

  ra.run = rb.run = 0;
  if(read(dpA[0], &ra, sizeof(ra)) != sizeof(ra))
    fail(name, "child A did not report");
  if(read(dpB[0], &rb, sizeof(rb)) != sizeof(rb))
    fail(name, "child B did not report");
  close(dpA[0]); close(dpB[0]);

  if(sample(pidA, A) < 0 || sample(pidB, B) < 0){
    fail(name, "procstat on a live child failed");
    close(hp[1]); wait(); wait();
    return;
  }
  if(sample(0, me1) < 0)
    fail(name, "procstat(0, buf) failed");

  show("A, nice 0", &ra);
  show("B, nice 5", &rb);
  printf(1, "  vruntime read by the parent with procstat(pid):"
            " A %d B %d\n", A[0], B[0]);

  slices = (ra.run + rb.run + 4) / 8;
  spread = (A[0] + B[0]) > 0 ? r_absdiff(A[0], B[0]) * 200 / (A[0] + B[0]) : 100;

  r_header();
  r_check(&failures, "A and B got the whole window", ra.run + rb.run,
        W_MIN, window - WINDOW_SLACK, 0,
        "something else in the system was runnable during the window");

  r_check(&failures, "wait_A == run_B          (Q2-3)", ra.d[3],
        W_RANGE, rb.run - WAIT_UNDER, rb.run + WAIT_OVER,
        "A should be waiting exactly while B runs: wait is added at the wrong "
        "moment, or a path that writes RUNNABLE does not record the tick");
  r_check(&failures, "wait_B == run_A          (Q2-3)", rb.d[3],
        W_RANGE, ra.run - WAIT_UNDER, ra.run + WAIT_OVER,
        "B should be waiting exactly while A runs: wait is added at the wrong "
        "moment, or a path that writes RUNNABLE does not record the tick");

  r_check(&failures, "notpicked_A == picked_B  (Q2-2)", ra.d[2],
        W_NEAR, rb.d[1], PICK_SLACK,
        "every turn given to B is exactly one choice A lost: notpicked is not "
        "counted where the scheduler picks a winner");
  r_check(&failures, "notpicked_B == picked_A  (Q2-2)", rb.d[2],
        W_NEAR, ra.d[1], PICK_SLACK,
        "every turn given to A is exactly one choice B lost: notpicked is not "
        "counted where the scheduler picks a winner");

  r_check(&failures, "sleeping parent lost nothing (Q2-2)", me1[2] - me0[2],
        W_MAX, PARENT_SLACK, 0,
        "the parent was asleep on a pipe and never RUNNABLE, so it lost no "
        "choice: notpicked counts processes that were not RUNNABLE");
  r_check(&failures, "sleeping parent waited nothing (Q2-3)", me1[3] - me0[3],
        W_MAX, 2 * PARENT_SLACK, 0,
        "the parent was asleep on a pipe, not ready and waiting: wait counts "
        "time spent asleep");

  r_check(&failures, "run_A >= 2 * run_B (nice 0 vs 5)", ra.run,
        W_MIN, 2 * rb.run, 0,
        "nice 0 should get about 3x the CPU of nice 5 -- adding statistics "
        "changed your scheduling");
  r_check(&failures, "picks vs 8-tick slices   (Q2-1)", ra.d[1] + rb.d[1],
        W_RANGE, slices / 2, slices * 2 + 2,
        "with two runnable processes a slice is CFS_LATENCY/2 = 8 ticks, so "
        "the number of picks should be about one eighth of the ticks");
  r_check(&failures, "vruntime A vs B spread, percent", spread, W_MAX, 50, 0,
        "both children were placed at min_vruntime and CFS equalises virtual "
        "time, so these should end close together: buf[0] is not the value "
        "CFS compares");

  close(hp[1]);
  wait();
  wait();
}

// A sleeper is killed while a CPU-bound sibling keeps the CPU busy. The
// sleeper becomes RUNNABLE inside kill(), is given the CPU within a tick or
// two, and exits on its way back to user space.
static void
kill_phase(void)
{
  char *name = "Q2 kill";
  int rp[2], hp[2], S[NSTAT];
  int pidS, pidW;
  char c;

  if(pipe(rp) < 0 || pipe(hp) < 0){ fail(name, "pipe failed"); return; }

  pidS = fork();
  if(pidS < 0){ fail(name, "fork failed"); return; }
  if(pidS == 0){
    close(rp[0]); close(hp[1]);
    c = 'r'; write(rp[1], &c, 1); close(rp[1]);
    read(hp[0], &c, 1);             // sleeps here until killed
    exit();
  }
  pidW = fork();
  if(pidW < 0){ fail(name, "fork failed"); return; }
  if(pidW == 0){
    close(rp[0]); close(rp[1]); close(hp[1]); close(hp[0]);
    for(;;) unit();                 // keeps the CPU busy while S sleeps
  }
  close(rp[1]); close(hp[0]);
  read(rp[0], &c, 1); close(rp[0]); // S has reached the pipe

  sleep(20);                        // S sleeps about 20 ticks; W runs
  kill(pidS);
  sleep(4);                         // S wakes, is given the CPU, exits

  if(sample(pidS, S) < 0)
    fail(name, "the killed child was not found as a zombie");
  else {
    printf(1, "  S, killed after sleeping ~20 ticks: picked %d"
              " notpicked %d wait %d\n",
           S[1], S[2], S[3]);
    r_check(&failures, "killed sleeper wait      (Q2-3)", S[3], W_MAX, 10, 0,
          "its wait includes the ~20 ticks it slept: kill() did not record "
          "the tick at which it became RUNNABLE");
    r_check(&failures, "killed sleeper picked    (Q2-1)", S[1], W_RANGE, 2, 4,
          "it is picked twice, once to reach the pipe and once to die. Zero "
          "means picked is counted at yield(): the sleeper never yielded");
    r_check(&failures, "killed sleeper notpicked (Q2-2)", S[2], W_MAX, 4, 0,
          "it lost choices while it was asleep: notpicked counts processes "
          "that were not RUNNABLE");
  }
  kill(pidW);
  close(hp[1]);
  wait();
  wait();
}

int
main(int argc, char **argv)
{
  int begun;

  (void)argc; (void)argv;
  begun = uptime();

  pair_phase(0, 5, 240);
  kill_phase();
  setnice(0, 0);

  printf(1, "statquiz2: completed in %d ticks\n", uptime() - begun);
  if(failures == 0)
    printf(1, "STATQUIZ2: PASS\n");
  else
    printf(2, "STATQUIZ2: FAIL (%d checks)\n", failures);
  exit();
}
