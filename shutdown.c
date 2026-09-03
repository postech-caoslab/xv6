// shutdown: power the machine off.

#include "types.h"
#include "stat.h"
#include "user.h"

int
main(void)
{
  shutdown();
  printf(2, "shutdown: failed\n");
  exit();
}
