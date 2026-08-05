#include "types.h"
#include "stat.h"
#include "user.h"

static void
putc(int fd, char c)
{
  write(fd, &c, 1);
}

static void
printint(int fd, uint64 xx, int base, int sgn)
{
  static char digits[] = "0123456789ABCDEF";
  char buf[24];
  int i, neg;
  uint64 x;

  neg = 0;
  if(sgn && (long)xx < 0){
    neg = 1;
    x = -xx;
  } else {
    x = xx;
  }

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);
  if(neg)
    buf[i++] = '-';

  while(--i >= 0)
    putc(fd, buf[i]);
}

// Print to the given fd. Only understands %d, %x, %p, %s, %c.
void
printf(int fd, const char *fmt, ...)
{
  __builtin_va_list ap;
  char *s;
  int c, i, state;

  state = 0;
  __builtin_va_start(ap, fmt);
  for(i = 0; fmt[i]; i++){
    c = fmt[i] & 0xff;
    if(state == 0){
      if(c == '%'){
        state = '%';
      } else {
        putc(fd, c);
      }
    } else if(state == '%'){
      if(c == 'd'){
        printint(fd, __builtin_va_arg(ap, int), 10, 1);
      } else if(c == 'x'){
        printint(fd, __builtin_va_arg(ap, uint), 16, 0);
      } else if(c == 'p'){
        printint(fd, __builtin_va_arg(ap, uint64), 16, 0);
      } else if(c == 's'){
        s = __builtin_va_arg(ap, char*);
        if(s == 0)
          s = "(null)";
        while(*s != 0){
          putc(fd, *s);
          s++;
        }
      } else if(c == 'c'){
        putc(fd, __builtin_va_arg(ap, int));
      } else if(c == '%'){
        putc(fd, c);
      } else {
        // Unknown % sequence.  Print it to draw attention.
        putc(fd, '%');
        putc(fd, c);
      }
      state = 0;
    }
  }
  __builtin_va_end(ap);
}
