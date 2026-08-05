#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint64 argc, sz, sz1, sp, argvaddr, oldsz;
  uintp ustack[MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }
  ilock(ip);
  pgdir = 0;
  sz = 0;

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0)
    goto bad;

  // Load program into memory.
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if((sz1 = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    sz = sz1;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz1 = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  sz = sz1;
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  sp = sz;

  // Push argument strings, then the argv array.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~(uintp)(sizeof(uintp)-1);
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;

  sp -= (argc+1) * sizeof(uintp);
  sp &= ~(uintp)15;  // 16-byte align the argv array
  argvaddr = sp;
  if(copyout(pgdir, sp, ustack, (argc+1)*sizeof(uintp)) < 0)
    goto bad;

  // Fake return PC; also makes %rsp % 16 == 8 at the entry of main(),
  // as if it had been reached by a call instruction.
  sp -= sizeof(uintp);
  ustack[0] = 0xFFFFFFFFFFFFFFFF;
  if(copyout(pgdir, sp, ustack, sizeof(uintp)) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  // Commit to the user image.
  oldpgdir = curproc->pgdir;
  oldsz = curproc->sz;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->rip = elf.entry;  // main
  curproc->tf->rsp = sp;
  curproc->tf->rdi = argc;       // main(argc, argv)
  curproc->tf->rsi = argvaddr;
  switchuvm(curproc);
  freevm(oldpgdir, oldsz);
  return 0;

 bad:
  if(pgdir)
    freevm(pgdir, sz);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}
