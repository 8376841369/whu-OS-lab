#include "riscv.h"
#include "lib/print.h"
#include "proc/proc.h"
#include "dev/uart.h"
volatile static int started = 0;

int main()
{
    if(mycpuid()==0)
    {
        uart_init();
        print_init();
        printf("\n");
        printf("xv6 kernel is booting\n");
        printf("\n");
    }
    if(mycpuid()==1)
    {
        uart_init();
        print_init();
        printf("\n");
        printf("cpu1 is booting\n");
        printf("\n");
    }
    if(mycpuid()==2)
    {
        uart_init();
        print_init();
        printf("\n");
        printf("cpu2 is booting\n");
        printf("\n");
    }
    while (1);    
}