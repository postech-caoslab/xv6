// swaptest: checks swap step by step on a 4MB machine.  DO NOT MODIFY.
//
// T1-T4 run in one worker process (they share its 4MB); T5 and T6 run in
// their own children.  Each test prints its input and expected result first,
// so if the kernel panics, the last "T" line shows which test was running.
// A failed test prints "check:" with the TODOs to look at.
// "trap 14 ... kill proc" lines in T6 are expected: those children must be killed.

#include "types.h"
#include "stat.h"
#include "user.h"

#define PGSIZE   4096
#define NPAGES   1024                // 4MB of heap on a 4MB machine
#define CHUNKPG  128                 // sbrk() 512KB at a time
#define NWORKER  4                   // T1-T4

static char *base;

static void
fill(int i)
{
  char *p = base + i*PGSIZE;
  *(int*)p = i ^ 0x5A5A5A5A;
  *(int*)(p + PGSIZE - 4) = i ^ (int)0xA5A5A5A5;
}

static int
check(int i)
{
  char *p = base + i*PGSIZE;
  return *(int*)p == (i ^ 0x5A5A5A5A) &&
         *(int*)(p + PGSIZE - 4) == (i ^ (int)0xA5A5A5A5);
}

static char *name[] = {
  "",
  "more memory than the machine has",
  "a swapped-out page comes back",
  "every page keeps its data",
  "fork copies swapped-out pages",
  "exit gives slots and frames back",
  "other page faults still kill the process",
};

// what to look at if the worker is killed during test n
static char *killhint[] = {
  "",
  "TODO 1-6: list_add(), list_pick(), allocuvm(), swap_out(), kalloc()",
  "TODO 8 trap(): a fault on a swapped-out page must call swap_in(); "
  "TODO 7 swap_in(): PTE flags, list_add()",
  "TODO 8 trap(), TODO 7 swap_in(): PTE flags, list_add()",
  "TODO 10 copyuvm()",
};

static void
header(int n, char *input, char *expected)
{
  printf(1, "T%d:\n", n);
  printf(1, "  input:    %s\n", input);
  printf(1, "  expected: %s\n", expected);
}

static int
verdict(int n, int ok, char *hint)
{
  if(!ok && hint)
    printf(1, "  check:    %s\n", hint);
  printf(1, "swaptest: %s: %s\n\n", name[n], ok ? "PASS" : "FAIL");
  return ok;
}

static void
report(int fd, int ok)
{
  char r = ok ? 'P' : 'F';
  write(fd, &r, 1);
}

// T1-T4
static void
worker(int fd)
{
  int i, n, ok, bad, cbad, pbad, pid, p[2];

  // T1: allocate more than memory
  header(1, "sbrk 1024 pages (4MB) on a 4MB machine (about 650 free frames)",
         "1024 pages");
  base = sbrk(0);
  for(n = 0; n < NPAGES; n += CHUNKPG)
    if(sbrk(CHUNKPG*PGSIZE) == (char*)-1)
      break;
  printf(1, "  observed: %d pages\n", n);
  ok = verdict(1, n == NPAGES,
               "TODO 1-6: list_add(), list_pick(), allocuvm(), swap_out(), kalloc()");
  report(fd, ok);
  if(!ok)
    exit();

  // T2: one page goes out and comes back
  header(2, "write all 1024 pages (page 0 first), then read page 0: it was swapped out",
         "page 0 has its value");
  for(i = 0; i < NPAGES; i++)
    fill(i);
  ok = check(0);
  printf(1, "  observed: page 0 %s\n", ok ? "has its value" : "is corrupt");
  report(fd, verdict(2, ok, "TODO 5 swap_out(), TODO 7 swap_in(): the PTE, the slot, the data"));

  // T3: all pages
  header(3, "read all 1024 pages forward, then backward", "0 corrupt pages");
  bad = 0;
  for(i = 0; i < NPAGES; i++)
    if(!check(i))
      bad++;
  for(i = NPAGES-1; i >= 0; i--)
    if(!check(i))
      bad++;
  printf(1, "  observed: %d corrupt pages\n", bad);
  report(fd, verdict(3, bad == 0,
         "TODO 5 swap_out() (change the PTE before swapwrite, keep the flags), "
         "TODO 7 swap_in() (swapread, PTE)"));

  // T4: fork with swapped-out pages
  header(4, "fork; the child reads every 7th page, then writes over it",
         "child: 0 corrupt pages, parent: 0 changed pages");
  cbad = -1;
  pipe(p);
  pid = fork();
  if(pid == 0){
    close(p[0]);
    cbad = 0;
    for(i = 0; i < NPAGES; i += 7)
      if(!check(i))
        cbad++;
    for(i = 0; i < NPAGES; i += 7)
      *(int*)(base + i*PGSIZE) = 0x12345678;
    write(p[1], &cbad, sizeof cbad);
    exit();
  }
  close(p[1]);
  if(pid > 0){
    wait();
    if(read(p[0], &cbad, sizeof cbad) != sizeof cbad)
      cbad = -1;                        // the child was killed
  }
  close(p[0]);
  pbad = 0;
  for(i = 0; i < NPAGES; i += 7)
    if(!check(i))
      pbad++;
  if(pid < 0)
    printf(1, "  observed: fork failed\n");
  else if(cbad < 0)
    printf(1, "  observed: the child was killed, parent: %d changed pages\n", pbad);
  else
    printf(1, "  observed: child: %d corrupt pages, parent: %d changed pages\n", cbad, pbad);
  report(fd, verdict(4, pid > 0 && cbad == 0 && pbad == 0,
         "TODO 10 copyuvm(): copy the slot of a swapped-out page; "
         "put the child's page on the use_list"));
  exit();
}

// run f() in a child; 1 if the child was killed before it said "alive"
static int
killed(void (*f)(void))
{
  int p[2];
  char c;

  pipe(p);
  if(fork() == 0){
    close(p[0]);
    f();
    write(p[1], "a", 1);
    exit();
  }
  close(p[1]);
  wait();
  c = 0;
  read(p[0], &c, 1);
  close(p[0]);
  return c != 'a';
}

static void
read_kernel(void)
{
  volatile char *k = (char*)0xFFFFFFFF80000000;   // KERNBASE
  char c = *k;
  (void)c;
}

static void
read_freed(void)
{
  volatile char *a = sbrk(PGSIZE);
  char c;
  a[0] = 1;
  sbrk(-PGSIZE);
  c = a[0];                             // its PTE is 0 now
  (void)c;
}

static void
fill_and_exit(void)
{
  int i;

  base = sbrk(0);
  for(i = 0; i < NPAGES; i += CHUNKPG)
    if(sbrk(CHUNKPG*PGSIZE) == (char*)-1)
      exit();
  for(i = 0; i < NPAGES; i++)
    fill(i);
}

int
main(void)
{
  int p[2], got, i, nfail, s0, s1, f0, f1, k1, k2;
  char r[NWORKER];

  // T1-T4 in a worker
  pipe(p);
  if(fork() == 0){
    close(p[0]);
    worker(p[1]);
    exit();
  }
  close(p[1]);
  got = 0;
  while(got < NWORKER && read(p[0], &r[got], 1) == 1)
    got++;
  wait();
  close(p[0]);
  nfail = 0;
  for(i = 0; i < got; i++)
    if(r[i] != 'P')
      nfail++;
  if(got < NWORKER){
    i = got;                            // the test that did not finish
    if(!(got == 1 && r[0] != 'P')){     // not a planned stop after T1
      printf(1, "  observed: the process was killed\n");
      verdict(got + 1, 0, killhint[got + 1]);
      nfail++;
      i = got + 1;
    }
    for(; i < NWORKER; i++){
      printf(1, "T%d:\n  not run (the worker stopped)\n", i + 1);
      printf(1, "swaptest: %s: FAIL\n\n", name[i + 1]);
      nfail++;
    }
  }

  // T5: exit frees slots and frames
  header(5, "a child fills 4MB, then exits",
         "slots in use and free frames are back to where they started");
  s0 = swapslots();
  f0 = freemem();
  killed(fill_and_exit);
  s1 = swapslots();
  f1 = freemem();
  printf(1, "  observed: slots in use %d -> %d, free frames %d -> %d\n", s0, s1, f0, f1);
  if(!verdict(5, s1 == s0 && f1 == f0,
              "TODO 9 deallocuvm(): free the slot of a swapped-out page (slotfree), "
              "take a freed page off the use_list (list_remove)"))
    nfail++;

  // T6: page faults that are not swap
  header(6, "one child reads kernel memory (KERNBASE), another reads a page it freed",
         "both children are killed");
  k1 = killed(read_kernel);
  k2 = killed(read_freed);
  printf(1, "  observed: kernel memory: %s, freed page: %s\n",
         k1 ? "killed" : "NOT killed", k2 ? "killed" : "NOT killed");
  if(!verdict(6, k1 && k2,
              "TODO 8 trap(): call swap_in() only for a swapped-out page "
              "(user address, PTE_P = 0, PTE != 0)"))
    nfail++;

  if(nfail == 0)
    printf(1, "swaptest: PASS (0 failed)\n");
  else
    printf(1, "swaptest: FAIL (%d failed)\n", nfail);
  exit();
}
