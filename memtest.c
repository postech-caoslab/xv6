#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int i, sz;

  if (argc != 2) {
    printf(1, "Usage: %s <memory to allocate in bytes>\n", argv[0]);
    printf(1, " e.g., %s 1048576 (allocate 1 MiB)\n", argv[0]);
    printf(1, " e.g., %s 65536   (allocate 64 KiB)\n", argv[0]);
    exit();
  }

  sz = atoi(argv[1]);

  uchar *ptr = malloc(sz);

  // Fill the memory
  for (i = 0; i < sz; i++) {
    ptr[i] = i % 255;
  }

  // Explicit free is unnecessary here

  exit();
}
