/* main.c -- runs the tests and prints the score. DO NOT MODIFY.
 * The tests are in test_swap.c: test 1 = Quiz 1, ..., test 4 = Quiz 4.
 *
 *   ./swap_sim        all tests        ./swap_sim 4   one test, with details
 *
 * Each test runs in its own process: a crash or an endless loop in one test
 * does not affect the others.  Each test is scored on its own.  A test passes
 * only if it runs to its end: an exit() in the quiz code fails it. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include "test.h"

#define TIMEOUT 2                           /* seconds per test */
#define DONE    42                          /* exit status: the test returned and passed */

static char out[4096];                      /* message and details of a test */
static int  outn;
int         verbose;                        /* one test: print the details */

void
say(const char *fmt, ...)
{
  va_list ap;

  if(outn >= (int)sizeof out)
    return;
  va_start(ap, fmt);
  outn += vsnprintf(out + outn, sizeof out - outn, fmt, ap);
  va_end(ap);
  if(outn > (int)sizeof out)
    outn = sizeof out;
}

#define NTEST ntests

/* run one test in a child process; 1 if it passed */
static int
run(const struct test *t)
{
  int   fd[2], status, n, passed;
  pid_t pid;

  fflush(stdout);
  if(pipe(fd) < 0 || (pid = fork()) < 0){
    perror("main");
    exit(2);
  }
  if(pid == 0){                             /* child: run the test */
    int r;

    close(fd[0]);
    outn = 0;
    setvbuf(stdout, 0, _IONBF, 0);          /* keep the student's printf()s */
    alarm(TIMEOUT);
    r = t->fn();
    if(write(fd[1], out, outn) < 0)
      _exit(2);
    _exit(r? 1 : DONE);                     /* not 0: an exit(0) is not a pass */
  }

  close(fd[1]);
  outn = 0;
  while(outn < (int)sizeof out - 1 && (n = (int)read(fd[0], out + outn, sizeof out - 1 - outn)) > 0)
    outn += n;
  out[outn] = 0;
  close(fd[0]);
  waitpid(pid, &status, 0);

  passed = WIFEXITED(status) && WEXITSTATUS(status) == DONE;
  printf("  [%s] %-43s %2d/%-2d %s\n", t->id, t->name, passed? t->pts : 0, t->pts,
         passed? "PASS" : "FAIL");
  if(WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM)
    printf("        timed out (an endless loop?)\n");
  else if(WIFSIGNALED(status))
    printf("        crashed (%s)\n", strsignal(WTERMSIG(status)));
  else if(!passed && WEXITSTATUS(status) != 1)
    printf("        the test did not finish (exit() in the quiz code?)\n");
  if(outn > 0){                             /* message / details, indented */
    char *line = strtok(out, "\n");

    for(; line; line = strtok(0, "\n")){
      int len = (int)strlen(line);

      while(len > 0 && line[len - 1] == ' ')  /* no trailing spaces */
        line[--len] = 0;
      if(strcmp(line, "-") != 0)
        printf("        %s\n", line);
    }
  }
  return passed;
}

int
main(int argc, char *argv[])
{
  int i, score = 0, total = 0, all = 1, ran = 0;

  printf("%s\n", title);
  for(i = 0; i < NTEST; i++){
    if(argc > 1 && strcmp(argv[1], tests[i].id) != 0)
      continue;
    verbose = argc > 1;
    ran++;
    total += tests[i].pts;
    if(run(&tests[i]))
      score += tests[i].pts;
    else
      all = 0;
  }
  if(ran == 0){
    printf("  no test named %s\n", argv[1]);
    return 2;
  }
  printf("  SCORE : %d/%d\n", score, total);
  printf("  RESULT: %s\n", all? "PASS" : "FAIL");
  if(argc == 1 && trace_id)
    printf("  (run %s %s to compare the full trace: expected vs yours)\n", argv[0], trace_id);
  return all? 0 : 1;
}
