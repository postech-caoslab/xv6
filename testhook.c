// Course-provided test accounting -- DO NOT MODIFY. Replaced when grading.
// Counts the timer ticks each process runs, independently of the scheduler.
#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

#define TH_SLOTS 1024

static struct { int pid; uint ticks; } th[TH_SLOTS];

// Called from trap() on each timer tick taken by a running process.
void
test_tick(void)
{
  int pid = myproc()->pid;
  int i = pid % TH_SLOTS;

  if(th[i].pid != pid){
    th[i].pid = pid;
    th[i].ticks = 0;
  }
  th[i].ticks++;
}

// Read-only measurement syscall used by schedticks.
long
sys_getrunticks(void)
{
  int pid = myproc()->pid;
  int i = pid % TH_SLOTS;

  return th[i].pid == pid ? th[i].ticks : 0;
}
