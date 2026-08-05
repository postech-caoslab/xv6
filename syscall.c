#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "syscall.h"

// User code makes a system call with INT T_SYSCALL.
// System call number in %rax.
// Arguments in %rdi, %rsi, %rdx, %rcx, per the x86-64 C calling
// convention (the int instruction preserves all registers, so the
// arguments arrive in the trap frame).

// Fetch the int at addr from the current process.
int
fetchint(uintp addr, int *ip)
{
  struct proc *curproc = myproc();

  if(addr >= curproc->sz || addr+4 > curproc->sz)
    return -1;
  *ip = *(int*)(addr);
  return 0;
}

// Fetch the pointer-sized word at addr from the current process.
int
fetchuintp(uintp addr, uintp *ip)
{
  struct proc *curproc = myproc();

  if(addr >= curproc->sz || addr+sizeof(uintp) > curproc->sz)
    return -1;
  *ip = *(uintp*)(addr);
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Doesn't actually copy the string - just sets *pp to point at it.
// Returns length of string, not including nul.
int
fetchstr(uintp addr, char **pp)
{
  char *s, *ep;
  struct proc *curproc = myproc();

  if(addr >= curproc->sz)
    return -1;
  *pp = (char*)addr;
  ep = (char*)curproc->sz;
  for(s = *pp; s < ep; s++){
    if(*s == 0)
      return s - *pp;
  }
  return -1;
}

// Return the nth system call argument (raw register value).
static uintp
argraw(int n)
{
  struct trapframe *tf = myproc()->tf;

  switch(n){
  case 0:
    return tf->rdi;
  case 1:
    return tf->rsi;
  case 2:
    return tf->rdx;
  case 3:
    return tf->rcx;
  }
  panic("argraw");
}

// Fetch the nth 32-bit system call argument.
int
argint(int n, int *ip)
{
  *ip = argraw(n);
  return 0;
}

// Fetch the nth system call argument as a pointer-sized integer.
int
argaddr(int n, uintp *ip)
{
  *ip = argraw(n);
  return 0;
}

// Fetch the nth word-sized system call argument as a pointer
// to a block of memory of size bytes.  Check that the pointer
// lies within the process address space.
int
argptr(int n, char **pp, int size)
{
  uintp i;
  struct proc *curproc = myproc();

  i = argraw(n);
  if(size < 0 || i >= curproc->sz || i+size > curproc->sz)
    return -1;
  *pp = (char*)i;
  return 0;
}

// Fetch the nth word-sized system call argument as a string pointer.
// Check that the pointer is valid and the string is nul-terminated.
// (There is no shared writable memory, so the string can't change
// between this check and being used by the kernel.)
int
argstr(int n, char **pp)
{
  return fetchstr(argraw(n), pp);
}

extern long sys_chdir(void);
extern long sys_close(void);
extern long sys_dup(void);
extern long sys_exec(void);
extern long sys_exit(void);
extern long sys_fork(void);
extern long sys_fstat(void);
extern long sys_getpid(void);
extern long sys_kill(void);
extern long sys_link(void);
extern long sys_mkdir(void);
extern long sys_mknod(void);
extern long sys_open(void);
extern long sys_pipe(void);
extern long sys_read(void);
extern long sys_sbrk(void);
extern long sys_sleep(void);
extern long sys_unlink(void);
extern long sys_wait(void);
extern long sys_write(void);
extern long sys_uptime(void);

static long (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
};

void
syscall(void)
{
  int num;
  struct proc *curproc = myproc();

  num = curproc->tf->rax;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    curproc->tf->rax = syscalls[num]();
  } else {
    cprintf("%d %s: unknown sys call %d\n",
            curproc->pid, curproc->name, num);
    curproc->tf->rax = -1;
  }
}
