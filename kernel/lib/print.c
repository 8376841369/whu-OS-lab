// 标准输出和报错机制
#include <stdarg.h>
#include "lib/print.h"
#include "lib/lock.h"
#include "dev/uart.h"

volatile int panicked = 0;

static spinlock_t print_lk;

static char digits[] = "0123456789abcdef";

void print_init(void)
{
    spinlock_init(&print_lk,"print_lk");//初始化自旋锁；

}

void print_int(long long xx,int base,int sign)
{
    char buf[20];
    int i;
    unsigned long long x;

    if(sign && (sign = (xx < 0)))
    x = -xx;
    else
    x = xx;

    //进制转换
    i = 0;
    do {
    buf[i++] = digits[x % base];
     } while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    uart_putc_sync(buf[i]);
}

void print_ptr(uint64 x)
{
    int i;
    uart_putc_sync('0');
    uart_putc_sync('x');

    for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    uart_putc_sync(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the console. only understands %d, %x, %p, %s.
void printf(const char *fmt, ...)
{
    if(!fmt) return;

    va_list ap;
    int i, cx,c0,c1,c2;
    char *s;

    if(panicked==0)
    {
        spinlock_acquire(&print_lk);
    }

    va_start(ap, fmt);

    for(i = 0; (cx = fmt[i] & 0xff) != 0; i++){
    if(cx != '%'){
      uart_putc_sync(cx);
      continue;
    }
    i++;
    c0 = fmt[i+0] & 0xff;
    c1 = c2 = 0;
    if(c0) c1 = fmt[i+1] & 0xff;
    if(c1) c2 = fmt[i+2] & 0xff;
    if(c0 == 'd'){
      print_int(va_arg(ap, int), 10, 1);
    } else if(c0 == 'l' && c1 == 'd'){
      print_int(va_arg(ap, uint64), 10, 1);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'd'){
      print_int(va_arg(ap, uint64), 10, 1);
      i += 2;
    } else if(c0 == 'u'){
      print_int(va_arg(ap, uint32), 10, 0);
    } else if(c0 == 'l' && c1 == 'u'){
      print_int(va_arg(ap, uint64), 10, 0);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'u'){
      print_int(va_arg(ap, uint64), 10, 0);
      i += 2;
    } else if(c0 == 'x'){
      print_int(va_arg(ap, uint32), 16, 0);
    } else if(c0 == 'l' && c1 == 'x'){
      print_int(va_arg(ap, uint64), 16, 0);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'x'){
      print_int(va_arg(ap, uint64), 16, 0);
      i += 2;
    } else if(c0 == 'p'){
      print_ptr(va_arg(ap, uint64));
    } else if(c0 == 'c'){
      uart_putc_sync(va_arg(ap, int));
    } else if(c0 == 's'){
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        uart_putc_sync(*s);
    } else if(c0 == '%'){
      uart_putc_sync('%');
    } else if(c0 == 0){
      break;
    } else {
      // Print unknown % sequence to draw attention.
      uart_putc_sync('%');
      uart_putc_sync(c0);
    }

  }
  va_end(ap);

  if(panicked == 0)
    spinlock_release(&print_lk);

  return ;

}

void panic(const char *s)
{
  panicked = 1;
  printf("panic: ");
  printf("%s\n", s);
  panicked = 1; // freeze uart output from other CPUs
  for(;;)
    ;
}

void assert(bool condition, const char* warning)
{
    if (!condition) {
        // 如果没有提供具体提示，就给一个默认信息
        if (warning && *warning)
            panic(warning);
        else
            panic("assertion failed");
    }
    // condition 为真时什么都不做
}
