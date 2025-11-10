
// in initcode.c
#include "sys.h"

int main()
{
    long long heap_top = syscall(SYS_brk, 0);

    heap_top = syscall(SYS_brk, heap_top + 4096 * 10);

    heap_top = syscall(SYS_brk, heap_top - 4096 * 5);

    syscall(SYS_print, "\nuser begin\n");
    while(1);
    return 0;
}