// statvruntime -- is buf[0] really a virtual runtime?
//
// GIVEN. Do not modify.
//
// This is the whole of Quiz 1's check, and it needs nothing from Quiz 2: it
// runs one process, alone, and watches its own buf[0] against the run ticks
// the skeleton charged it.
//
//
// ---- why an exact number, and not a ratio --------------------------------
//
// The absolute value of a virtual runtime is unverifiable from user space: a
// process starts at whatever min_vruntime happened to be when it forked, and
// the value wraps. So this measures the *change* in buf[0], not its value.
//
// The change is exact, because the unit is not a free choice. The pre-project
// quiz grades cfs_delta_v(1, 0) == 1024, which fixes one tick at nice 0 at
// 1024 units and therefore every nice value at 1024 * 1024 / weight:
//
//   nice   0   1048576 /  1024  =  1024        per run tick
//   nice   5   1048576 /   335  =  3130
//   nice  10   1048576 /   110  =  9532
//   nice -20   1048576 / 88761  =    11
//
// Run ticks come from getrunticks(), which the course skeleton maintains in
// trap.c, so nothing written for this quiz appears on both sides of the check.
//
// Ticks are deterministic -- preemption is tick arithmetic and the slice is
// whole ticks -- so these numbers reproduce exactly, run after run.
//
// The nice -20 leg is the truncation trap: computing 1024/88761 in integer
// arithmetic gives 0, the heaviest process never advances, and a process whose
// virtual runtime never advances never gives up the CPU.
//
// Usage: statvruntime

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
#define W_EQ     6         // exactly a

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
  case W_EQ:    ok = got == a; break;
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
  case W_EQ:    printf(1, "%d", a);         n = r_numlen(a); break;
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

#define SLICE   16         // CFS_LATENCY: a lone process gets the whole period
#define WARM    8          // run ticks discarded after a nice change
#define LEG     80         // run ticks measured per nice level
#define NEGLEG  60         // run ticks for the nice -20 leg

// One run tick is worth exactly this much, per the table above.
#define U0     1024
#define U5     3130
#define U10    9532
#define UN       11

static int failures;
static volatile int sink;

static void
fail(char *why)
{
  failures++;
  printf(2, "statvruntime: FAIL: %s\n", why);
}

// One short unit of work. Short on purpose, so a loop can notice a tick
// boundary soon after it happens.
static void
unit(void)
{
  int i, s = 0;

  for(i = 0; i < 500; i++)
    s += i ^ (s >> 3);
  sink = s;
}

// Zero the buffer before every call, so a slot the kernel does not write yet
// reads as 0 rather than as whatever was on the stack.
static int
sample(int pid, int *buf)
{
  int i;

  for(i = 0; i < NSTAT; i++)
    buf[i] = 0;
  return procstat(pid, buf);
}

// One leg's row. `got` is the number the kernel actually reported -- the change
// in buf[0] over the leg -- and `want` is the run ticks the skeleton charged
// times the unit that nice value fixes, so the whole derivation is in the row
// and nothing has to be divided out by eye.
//
// The slack is 2% of the expected value, or one unit per run tick, whichever
// is larger. That second term is what matters at nice -20: charging tick by
// tick truncates 1048576/88761 to 11 and loses 0.81 units every tick, while
// charging several ticks at once and dividing keeps them. Both are correct,
// and one unit per tick bounds the gap between them.
static void
checkleg(int nice, int dv, int dr, int unit, char *why)
{
  int want = dr * unit;
  int slack = want / 50;
  int n;

  if(slack < dr)
    slack = dr;

  printf(1, "  buf[0] at nice %d = %d x %d", nice, dr, unit);
  n = 17 + r_numlen(nice) + 3 + r_numlen(dr) + 3 + r_numlen(unit);
  while(n++ < R_WHAT + 2)
    printf(1, " ");

  r_padnum(dv, R_GOT);
  printf(1, "  ");
  printf(1, "%d +-%d", want, slack);
  n = r_numlen(want) + 3 + r_numlen(slack);
  while(n++ < R_WANT)
    printf(1, " ");

  if(r_absdiff(dv, want) <= slack){
    printf(1, "  ok\n");
  } else {
    printf(1, "  FAIL\n");
    failures++;
    printf(1, "     -> %s\n", why);
  }
}

// buf[0] per run tick, in 1/256 units, computed so that neither a coarse nor a
// very fine unit overflows an int. Used only for the printed ratios.
static int
rate(int dv, int dr)
{
  if(dr <= 0 || dv < 0)
    return 0;
  return (dv / dr) * 256 + ((dv % dr) * 256) / dr;
}

// hi/lo as a percentage, shifted down first so the multiply cannot overflow.
static int
ratio_pct(int hi, int lo)
{
  while(hi > (1 << 20) || lo > (1 << 20)){
    hi >>= 1;
    lo >>= 1;
  }
  if(lo <= 0)
    return -1;
  return (hi * 100) / lo;
}

// Burn CPU until getrunticks() reaches `target`.
static void
burn(int target)
{
  while(getrunticks() < target)
    unit();
}

// Wait for buf[0] to move, then record it and the run-tick count at that
// instant. Sampling on a charge boundary is what makes the measurement
// independent of *when* a kernel charges virtual time: one that charges every
// tick and one that charges only at the end of a slice both give the same
// answer, because neither endpoint is left holding an uncharged backlog.
//
// Returns 0 if it moved, 1 if it did not move within a whole slice.
static int
snap(int *v, int *r)
{
  int buf[NSTAT], v0, deadline;

  if(sample(0, buf) < 0)
    return -1;
  v0 = buf[0];
  deadline = getrunticks() + SLICE + 4;
  while(getrunticks() < deadline){
    unit();
    if(sample(0, buf) < 0)
      return -1;
    if(buf[0] != v0){
      *v = buf[0];
      *r = getrunticks();
      return 0;
    }
  }
  *v = v0;
  *r = getrunticks();
  return 1;
}

// Measure one leg: set nice, throw away a warm-up, then measure buf[0] against
// run ticks between two charge boundaries.
//
// The warm-up matters. A kernel that changes a process's weight without first
// charging the time already run at the old weight mis-bills up to one slice;
// discarding the first few ticks after setnice() puts that error outside the
// measurement.
static int
leg(int nice, int runticks, int *dv, int *dr)
{
  int v0, r0, v1, r1;

  if(setnice(0, nice) < 0){
    fail("setnice was rejected");
    return -1;
  }
  burn(getrunticks() + WARM);

  if(snap(&v0, &r0) < 0){
    fail("procstat(0, buf) failed mid-measurement");
    return -1;
  }
  burn(r0 + runticks);
  if(snap(&v1, &r1) < 0){
    fail("procstat(0, buf) failed mid-measurement");
    return -1;
  }

  *dv = v1 - v0;
  *dr = r1 - r0;
  if(*dr <= 0){
    fail("no run ticks were charged to this process");
    return -1;
  }
  return 0;
}

int
main(int argc, char **argv)
{
  int buf[NSTAT], accepted, badpid, begun;
  int dv0, dr0, dv5, dr5, dv10, dr10, dvn, drn;
  int p5, p10;
  char *top;

  (void)argc; (void)argv;
  begun = uptime();
  printf(1, "statvruntime: one process alone, at nice 0, 5, 10 and -20\n");

  accepted = sample(0, buf);
  if(accepted < 0){
    fail("procstat(0, buf) was rejected -- the syscall is not wired up, "
         "and nothing else can be checked");
    printf(2, "STATVRUNTIME: FAIL (%d checks)\n", failures);
    exit();
  }
  badpid = sample(999999, buf);

  // Only the *change* in buf[0] is measured, never its value. On a freshly
  // booted system nothing has been charged a whole tick yet, so min_vruntime
  // is still 0 and a process that has just forked legitimately reports 0.

  dv0 = dr0 = dv5 = dr5 = dv10 = dr10 = dvn = drn = 0;
  if(leg(0, LEG, &dv0, &dr0) == 0 &&
     leg(5, LEG, &dv5, &dr5) == 0 &&
     leg(10, LEG, &dv10, &dr10) == 0)
    leg(-20, NEGLEG, &dvn, &drn);
  setnice(0, 0);

  p5 = ratio_pct(rate(dv5, dr5), rate(dv0, dr0));
  p10 = ratio_pct(rate(dv10, dr10), rate(dv0, dr0));

  r_header();
  r_check(&failures, "procstat(0, buf) accepted", accepted, W_EQ, 0, 0,
          "procstat must return 0 when the process exists");
  r_check(&failures, "nonexistent pid rejected (-1)", badpid, W_EQ, -1, 0,
          "procstat must return -1 when no process has that pid");

  checkleg(0, dv0, dr0, U0,
           "one tick at nice 0 must be worth 1024 -- the value your "
           "cfs_delta_v(1, 0) already returns");
  checkleg(5, dv5, dr5, U5,
           "1048576 / 335: check that the weight divides the tick rather "
           "than multiplying it");
  checkleg(10, dv10, dr10, U10,
           "1048576 / 110: check that the weight divides the tick rather "
           "than multiplying it");
  checkleg(-20, dvn, drn, UN,
           "0 here means 1024/88761 truncated to zero, so the heaviest "
           "process never advances and never yields");

  // Printed as corroboration only. These are what stays true whatever unit a
  // kernel picks, so they say *why* the four rows above are what they are.
  r_info("rate(5)/rate(0), percent", p5, 306);
  r_info("rate(10)/rate(0), percent", p10, 931);

  // Not stated on the slides, so reported and not counted: a syscall that
  // writes four ints must validate space for four.
  top = sbrk(0);
  if(procstat(0, (int*)(top - sizeof(int))) != -1)
    r_note("argptr", "validated less than 4 * sizeof(int) -- a kernel that "
                     "writes past the end of user memory panics");

  setnice(0, 0);
  printf(1, "statvruntime: completed in %d ticks\n", uptime() - begun);
  if(failures == 0)
    printf(1, "STATVRUNTIME: PASS\n");
  else
    printf(2, "STATVRUNTIME: FAIL (%d checks)\n", failures);
  exit();
}
