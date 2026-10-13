// GIVEN TEST -- DO NOT REMOVE. DO NOT MODIFY.
// schedticks -- black-box CFS checks using allocated timer ticks.
//
// This program does not call sdump() or consume a scheduling event log.
// getrunticks() is neutral instrumentation supplied by the course skeleton:
// it counts timer interrupts that occurred while the caller was RUNNING.
//
// CFS is the only scheduler in this project, so every section here measures
// CFS. There is no policy to select and no switch to exercise.
//
// Trace 4 measures sleeping and waking, which the others never exercise: a
// process that blocks misses everyone else's progress, and the placement rule
// (Lecture 06 slide 33) is what stops it monopolising the CPU on the way back.

#include "types.h"
#include "stat.h"
#include "user.h"

#define MAXKIDS   40
#define WARMUP    20
#define WINDOW    240
// The wakeup test sleeps for longer than it then measures, so that a missing
// placement -- which buys the sleeper one tick of exclusive CPU for every
// tick it slept -- takes the whole measured window rather than a slice of it.
#define SLEEP_TICKS 120      // how long the sleeper blocks
#define WAKE_WINDOW 60       // how long both are measured after the wakeup

struct gate {
  int start;
  int end;
};

struct result {
  int slot;
  int ticks[3];
};

static volatile uint sink;
static int failures;
static int work_iterations = 3000;

static void
work(void)
{
  int i;

  for(i = 0; i < work_iterations; i++)
    sink = sink * 1664525u + (uint)i + 1013904223u;
}

static int
readn(int fd, void *buffer, int n)
{
  char *p = buffer;
  int got, total = 0;

  while(total < n){
    got = read(fd, p + total, n - total);
    if(got <= 0)
      return -1;
    total += got;
  }
  return 0;
}

static int
writen(int fd, void *buffer, int n)
{
  char *p = buffer;
  int put, total = 0;

  while(total < n){
    put = write(fd, p + total, n - total);
    if(put <= 0)
      return -1;
    total += put;
  }
  return 0;
}

static void
report(char *name, int pass)
{
  printf(pass ? 1 : 2, "schedticks: %s: %s\n", name,
         pass ? "PASS" : "FAIL");
  if(!pass)
    failures++;
}

static void
print_ticks(int *values, int n)
{
  int i;

  printf(1, "  allocated ticks:");
  for(i = 0; i < n; i++)
    printf(1, " %d", values[i]);
  printf(1, "\n");
}

static int
sum_ticks(int *values, int n)
{
  int i, total = 0;

  for(i = 0; i < n; i++)
    total += values[i];
  return total;
}

static void
print_percent10(int numerator, int denominator)
{
  int value = denominator > 0 ? numerator * 1000 / denominator : 0;

  printf(1, "%d.%d%%", value / 10, value % 10);
}

static void
print_shares(int *values, int n)
{
  int i, total = sum_ticks(values, n);

  printf(1, "  observed shares:");
  for(i = 0; i < n; i++){
    printf(1, " ");
    print_percent10(values[i], total);
  }
  printf(1, "\n");
}

static void
print_ratio100(int numerator, int denominator)
{
  int value = denominator > 0 ? numerator * 100 / denominator : 0;
  int fraction = value % 100;

  printf(1, "%d.", value / 100);
  if(fraction < 10)
    printf(1, "0");
  printf(1, "%d", fraction);
}

// Run n CPU-bound children over the same tick window. Children begin spinning
// before the measurement start, so fork order is outside the measured window.
static int
run_phase(int *nices, int n, int duration, int *values)
{
  int gatefd[2], outfd[2];
  int i, pid, made = 0;
  struct gate gate;
  struct result result;

  if(n > MAXKIDS || pipe(gatefd) < 0 || pipe(outfd) < 0)
    return -1;

  for(i = 0; i < n; i++){
    pid = fork();
    if(pid < 0)
      break;
    if(pid == 0){
      int before, after;

      close(gatefd[1]);
      close(outfd[0]);
      if(readn(gatefd[0], &gate, sizeof(gate)) < 0)
        exit();
      while(uptime() < gate.start)
        work();
      before = getrunticks();
      while(uptime() < gate.end)
        work();
      after = getrunticks();
      result.slot = i;
      result.ticks[0] = after - before;
      result.ticks[1] = result.ticks[2] = 0;
      writen(outfd[1], &result, sizeof(result));
      exit();
    }
    made++;
    if(setnice(pid, nices ? nices[i] : 0) < 0)
      printf(2, "schedticks: setnice failed for child %d\n", pid);
  }

  gate.start = uptime() + WARMUP;
  gate.end = gate.start + duration;
  for(i = 0; i < made; i++)
    if(writen(gatefd[1], &gate, sizeof(gate)) < 0)
      break;
  close(gatefd[0]);
  close(gatefd[1]);
  close(outfd[1]);

  for(i = 0; i < made; i++){
    if(readn(outfd[0], &result, sizeof(result)) < 0)
      break;
    if(result.slot >= 0 && result.slot < n)
      values[result.slot] = result.ticks[0];
  }
  close(outfd[0]);
  for(i = 0; i < made; i++)
    wait();
  return made == n ? 0 : -1;
}
static void
test_equal_cfs(void)
{
  int values[4] = {0, 0, 0, 0};
  int nices[4] = {0, 0, 0, 0};
  int i, min, max, pass;

  printf(1, "T1:\n");
  pass = run_phase(nices, 4, WINDOW, values) == 0;
  min = max = values[0];
  for(i = 1; i < 4; i++){
    if(values[i] < min) min = values[i];
    if(values[i] > max) max = values[i];
  }
  print_ticks(values, 4);
  print_shares(values, 4);
  printf(1, "  reference: 25.0%% 25.0%% 25.0%% 25.0%%\n");
  printf(1, "  tolerance: largest allocation <= 1.25 x smallest\n");
  // Broad enough for tick-boundary quantisation; exact CFS is much tighter.
  pass = pass && min > 0 && max * 100 <= min * 125;
  report("Trace 1: CFS equal-nice fairness", pass);
}

static void
test_weighted_cfs(void)
{
  int values[3] = {0, 0, 0};
  int nices[3] = {-5, 0, 5};
  int pass;

  printf(1, "\nT2:\n");
  printf(1, "  Nice values: nice -5, nice 0, nice 5\n");
  pass = run_phase(nices, 3, WINDOW * 2, values) == 0;
  print_ticks(values, 3);
  print_shares(values, 3);
  printf(1, "  reference shares: 69.7%% 22.9%% 7.4%%\n");
  printf(1, "  observed adjacent ratios: ");
  print_ratio100(values[0], values[1]);
  printf(1, " and ");
  print_ratio100(values[1], values[2]);
  printf(1, " (reference 3.05 and 3.06)\n");
  // Adjacent expected ratios are about 3.05. These bounds test the direction
  // and approximate proportionality without relying on a single exact trace.
  pass = pass && values[2] > 0 &&
         values[0] > values[1] && values[1] > values[2] &&
         values[0] * 10 >= values[1] * 23 &&
         values[0] * 10 <= values[1] * 45 &&
         values[1] * 10 >= values[2] * 23 &&
         values[1] * 10 <= values[2] * 45;
  report("Trace 2: CFS weighted nice share", pass);
}

static void
test_starvation(void)
{
  int values[32];
  int nices[32];
  int i, pass = 1, min, max;

  printf(1, "\nT3:\n");
  for(i = 0; i < 32; i++)
    values[i] = nices[i] = 0;
  if(run_phase(nices, 32, WINDOW, values) < 0)
    pass = 0;
  for(i = 0; i < 32; i++)
    if(values[i] <= 0)
      pass = 0;
  min = max = values[0];
  for(i = 1; i < 32; i++){
    if(values[i] < min) min = values[i];
    if(values[i] > max) max = values[i];
  }
  print_ticks(values, 32);
  printf(1, "  observed: min %d, max %d ticks\n",
         min, max);
  printf(1, "  reference: nominal 240-tick window / 32 = 7 or 8 ticks each\n");
  printf(1, "  acceptance: every child receives at least one allocated tick\n");
  report("Trace 3: CFS 32-process progress", pass);
}
// A child that sleeps and wakes repeatedly, against one that never sleeps.
// The sleeper cannot earn more than the CPU it asks for, and must not be
// punished for having slept either: over the window it should get roughly the
// share its duty cycle asks for, and certainly not zero and not everything.
static void
test_repeated_sleep(void)
{
  int gatefd[2], outfd[2];
  int i, pid, made = 0, pass = 1;
  int values[2];
  struct gate gate;
  struct result result;

  printf(1, "\nT4:\n");
  printf(1, "  pid 1 alternates 2 ticks asleep / 2 ticks running; pid 2 never sleeps\n");
  values[0] = values[1] = 0;
  if(pipe(gatefd) < 0 || pipe(outfd) < 0){
    report("Trace 4: CFS repeated sleeper", 0);
    return;
  }

  for(i = 0; i < 2; i++){
    pid = fork();
    if(pid < 0)
      break;
    if(pid == 0){
      int before, after;

      close(gatefd[1]);
      close(outfd[0]);
      if(readn(gatefd[0], &gate, sizeof(gate)) < 0)
        exit();
      while(uptime() < gate.start)
        work();
      before = getrunticks();
      if(i == 0){
        // Sleep 2 ticks, run 2 ticks, over and over: an I/O-ish process.
        // The deadline is taken AFTER the sleep -- sleep(2) advances uptime()
        // by at least 2 itself, so a deadline taken before it has already
        // passed and the process would never run at all.
        while(uptime() < gate.end){
          int until;

          sleep(2);
          until = uptime() + 2;
          while(uptime() < until && uptime() < gate.end)
            work();
        }
      } else {
        while(uptime() < gate.end)
          work();
      }
      after = getrunticks();
      result.slot = i;
      result.ticks[0] = after - before;
      result.ticks[1] = result.ticks[2] = 0;
      writen(outfd[1], &result, sizeof(result));
      exit();
    }
    made++;
  }

  gate.start = uptime() + WARMUP;
  gate.end = gate.start + WINDOW;
  for(i = 0; i < made; i++)
    if(writen(gatefd[1], &gate, sizeof(gate)) < 0)
      break;
  close(gatefd[0]);
  close(gatefd[1]);
  close(outfd[1]);

  for(i = 0; i < made; i++){
    if(readn(outfd[0], &result, sizeof(result)) < 0)
      break;
    if(result.slot >= 0 && result.slot < 2)
      values[result.slot] = result.ticks[0];
  }
  close(outfd[0]);
  for(i = 0; i < made; i++)
    wait();

  if(made != 2)
    pass = 0;
  print_ticks(values, 2);
  print_shares(values, 2);
  // It asks for about half its time and sleeps the rest, so it should collect
  // a real but minority share. Zero means the wakeup path loses it; a
  // majority means each wakeup buys it a monopoly.
  if(values[0] <= 0 || values[1] <= 0)
    pass = 0;
  else if(values[0] * 100 / (values[0] + values[1]) > 60)
    pass = 0;
  printf(1, "  acceptance: the sleeper gets some ticks and no more than 60%% of them\n");
  report("Trace 4: CFS repeated sleeper", pass);
}
int
main(int argc, char **argv)
{
  if(argc > 1){
    work_iterations = atoi(argv[1]);
    if(work_iterations < 100)
      work_iterations = 100;
    if(work_iterations > 12000)
      work_iterations = 12000;
  }
  test_equal_cfs();
  test_weighted_cfs();
  test_starvation();
  test_repeated_sleep();

  setnice(0, 0);
  printf(1, "\nschedticks: %s (%d failed)\n",
         failures ? "FAIL" : "PASS", failures);
  exit();
}
