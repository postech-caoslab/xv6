# Initial process execs /init.
# This code runs in user space.

#include "syscall.h"
#include "traps.h"


# exec(init, argv)
.globl start
start:
  mov $init, %edi
  mov $argv, %esi
  mov $SYS_exec, %eax
  int $T_SYSCALL

# for(;;) exit();
exit:
  mov $SYS_exit, %eax
  int $T_SYSCALL
  jmp exit

# char init[] = "/init\0";
init:
  .string "/init\0"

# char *argv[] = { init, 0 };
.p2align 3
argv:
  .quad init
  .quad 0
