// init: The initial user-level program

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char *shargv[] = { "sh", 0 };
char initprog[64];

// /initrc, if it exists and is non-empty, names a program to run once
// instead of the shell.  ("init" itself is taken by this binary, so the
// file is called initrc.)  Returns 1 if a program was named.
int
readinitrc(void)
{
  int fd, n, i;

  if((fd = open("initrc", O_RDONLY)) < 0)
    return 0;
  n = read(fd, initprog, sizeof(initprog) - 1);
  close(fd);
  if(n <= 0)
    return 0;
  initprog[n] = 0;

  // Keep the first whitespace-delimited word.
  for(i = 0; initprog[i]; i++){
    if(initprog[i] == ' ' || initprog[i] == '\t' || initprog[i] == '\n'
       || initprog[i] == '\r'){
      initprog[i] = 0;
      break;
    }
  }
  return initprog[0] != 0;
}

// Run prog once, wait for it, then power the machine off.
void
runinit(char *prog)
{
  char *argv[] = { prog, 0 };
  int pid, wpid;

  printf(1, "init: starting %s\n", prog);
  pid = fork();
  if(pid < 0){
    printf(1, "init: fork failed\n");
    exit();
  }
  if(pid == 0){
    exec(prog, argv);
    printf(1, "init: exec %s failed\n", prog);
    exit();
  }
  while((wpid=wait()) >= 0 && wpid != pid)
    ;
  printf(1, "init: %s finished, powering off\n", prog);
  shutdown();
  printf(1, "init: shutdown failed\n");
}

int
main(void)
{
  int pid, wpid;

  if(open("console", O_RDWR) < 0){
    mknod("console", 1, 1);
    open("console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  if(readinitrc())
    runinit(initprog);

  for(;;){
    printf(1, "init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf(1, "init: fork failed\n");
      exit();
    }
    if(pid == 0){
      exec("sh", shargv);
      printf(1, "init: exec sh failed\n");
      exit();
    }
    while((wpid=wait()) >= 0 && wpid != pid)
      printf(1, "zombie!\n");
  }
}
