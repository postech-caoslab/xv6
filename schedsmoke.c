// GIVEN TEST -- DO NOT REMOVE. DO NOT MODIFY.
// schedsmoke -- a structural smoke test for the CFS scheduler.
//
// This checks that the required pieces are present and that no process is
// lost: setnice is wired, ordinary processes run to completion, children at
// every nice value are reaped, and a sleeping process is woken.
//
// It deliberately does NOT measure fairness, nice-weight ratios, slice length
// or starvation bounds: that is what schedticks is for.
//
// Each section prints its input, the expected result and what was observed,
// then PASS or FAIL -- the same shape as schedticks.
//
// CFS is the only scheduler in this project, so there is no policy to select.

#include "types.h"
#include "stat.h"
#include "user.h"

#define NKIDS 4
#define NICE_WINDOW 24          // ticks the mixed-nice children run for

#define NCHECK 4                // one per workload below

static int failures;
static volatile uint work_sink;
static char *check_name[NCHECK];
static int check_pass[NCHECK];
static int nchecks;

static void
report(char *name, int pass)
{
  printf(pass ? 1 : 2, "schedsmoke: %s: %s\n", name, pass ? "PASS" : "FAIL");
  if(nchecks < NCHECK){
    check_name[nchecks] = name;
    check_pass[nchecks] = pass;
    nchecks++;
  }
  if(!pass)
    failures++;
}

// Bounded CPU work: enough to exercise timer preemption, but correctness does
// not depend on how the CPU is divided between children.
static void
do_work(void)
{
  uint i;

  for(i = 0; i < 1500000; i++)
    work_sink = work_sink * 1664525u + i + 1013904223u;
}

// setnice is the only scheduler system call this project adds. A pid of 0
// means the caller, and an unknown pid must be rejected rather than silently
// accepted. The clamp to [-20,19] is tested by schedticks, not here.
static void
test_setnice_api(void)
{
  int lower, restore, unknown;

  printf(1, "T1:\n");
  lower   = setnice(0, -5);
  restore = setnice(0, 0);
  unknown = setnice(99999, 0);

  printf(1, "  input:    setnice(0, -5), setnice(0, 0), setnice(99999, 0)\n");
  printf(1, "  expected: 0, 0, -1\n");
  printf(1, "  observed: %d, %d, %d\n", lower, restore, unknown);
  report("setnice system call", lower == 0 && restore == 0 && unknown == -1);
}

// Fork children, let them work and exit, and reap them all. The odd-pid
// children sleep briefly so the group does not start in lockstep; what is
// under test is that every child completes and is reaped, not how the CPU was
// divided between them.
static void
test_basic_workload(void)
{
  int i, pid, made = 0, reaped = 0;

  printf(1, "T2:\n");
  for(i = 0; i < NKIDS; i++){
    pid = fork();
    if(pid < 0)
      break;
    if(pid == 0){
      if((getpid() & 1) != 0)
        sleep(2);
      do_work();
      exit();
    }
    made++;
  }

  while(reaped < made){
    if(wait() < 0)
      break;
    reaped++;
  }

  printf(1, "  input:    %d children; odd pids sleep 2 ticks first, then each\n",
         NKIDS);
  printf(1, "            does work and exits. The parent calls wait()\n");
  printf(1, "  expected: forked %d, exited %d\n", NKIDS, NKIDS);
  printf(1, "  observed: forked %d, exited %d\n", made, reaped);
  report("no process lost across fork, work and exit",
         made == NKIDS && reaped == NKIDS);
}

// Children at different nice values, all runnable at once, stopping on the
// xv6 tick clock rather than a work count -- so an unequal CPU share does not
// fail this smoke test. A child that was heavily delayed simply observes that
// the end tick has passed and exits when it next runs. A child that is never
// scheduled at all is never reaped, and that is the failure.
static void
test_mixed_nice(void)
{
  int nices[NKIDS] = { -10, -3, 3, 10 };
  int i, pid, made = 0, reaped = 0, end;

  printf(1, "T3:\n");
  end = uptime() + NICE_WINDOW;
  for(i = 0; i < NKIDS; i++){
    pid = fork();
    if(pid < 0)
      break;
    if(pid == 0){
      if(setnice(0, nices[i]) < 0)
        exit();
      while(uptime() < end)
        do_work();
      exit();
    }
    made++;
  }

  while(reaped < made){
    if(wait() < 0)
      break;
    reaped++;
  }

  printf(1, "  input:    %d children at nice -10, -3, 3 and 10, all runnable,\n",
         NKIDS);
  printf(1, "            and exits after %d ticks\n", NICE_WINDOW);
  printf(1, "  expected: forked %d, exited %d\n", NKIDS, NKIDS);
  printf(1, "  observed: forked %d, exited %d\n", made, reaped);
  report("no child stranded at any nice value",
         made == NKIDS && reaped == NKIDS);
}

// A child that sleeps on a pipe must still be woken and reaped. Under CFS a
// waking process is placed at the minimum virtual runtime; if that placement
// is wrong the child can be starved outright, which shows up here as a hang
// or as wait() returning -1.
static void
test_sleeper(void)
{
  int fds[2], pid, reaped, i;
  char c = 'x';

  printf(1, "T4:\n");
  if(pipe(fds) < 0){
    printf(1, "  observed: pipe failed\n");
    report("a sleeping child is woken and reaped", 0);
    return;
  }
  pid = fork();
  if(pid < 0){
    printf(1, "  observed: fork failed\n");
    report("a sleeping child is woken and reaped", 0);
    return;
  }
  if(pid == 0){
    close(fds[1]);
    read(fds[0], &c, 1);          // blocks until the parent writes
    do_work();
    exit();
  }
  close(fds[0]);
  for(i = 0; i < 3; i++)
    do_work();                    // run while the child is asleep
  write(fds[1], &c, 1);
  close(fds[1]);
  reaped = wait();

  printf(1, "  input:    Child sleeps on I/O -> Parent writes the I/O\n");
  printf(1, "            and wait() -> Child is woken up, and exits ->\n");
  printf(1, "            Parent returns the exited child's pid\n");
  printf(1, "  expected: wait() returns %d, the woken child's pid\n", pid);
  printf(1, "  observed: wait() returned %d\n", reaped);
  report("a sleeping child is woken and reaped", reaped == pid);
}

int
main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  test_setnice_api();
  test_basic_workload();
  test_mixed_nice();
  test_sleeper();

  setnice(0, 0);
  if(nchecks < NCHECK)
    printf(2, "  %d of %d workloads did not report\n", NCHECK - nchecks, NCHECK);

  printf(1, "schedsmoke: %s (%d failed)\n", failures ? "FAIL" : "PASS",
         failures);
  exit();
}
