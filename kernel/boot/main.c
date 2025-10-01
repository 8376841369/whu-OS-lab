#include "riscv.h"
#include "lib/print.h"
#include "proc/proc.h"
#include "dev/uart.h"
#include "lib/lock.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "lib/string.h"
// volatile static int started = 0;
// volatile static int sum = 0;

// static struct spinlock sum_lock;

// int main()
// {
//     // if(mycpuid()==0)
//     // {
//     //     uart_init();
//     //     print_init();
//     //     printf("\n");
//     //     printf("xv6 kernel is booting\n");
//     //     printf("\n");
//     //     started = 1;
//     // }
//     // if(mycpuid()==1)
//     // {
//     //     uart_init();
//     //     print_init();
//     //     printf("\n");
//     //     printf("cpu1 is booting\n");
//     //     printf("\n");
//     // }
//     // if(mycpuid()==2)
//     // {
//     //     uart_init();
//     //     print_init();
//     //     printf("\n");
//     //     printf("cpu2 is booting\n");
//     //     printf("\n");
//     // }
    
//     // if(mycpuid()==3)
//     // {
//     //     uart_init();
//     //     print_init();
//     //     printf("\n");
//     //     printf("cpu3 is booting\n");
//     //     printf("\n");
//     // }
//     // while (1);   

//       int cpuid = r_tp();
//         if(cpuid == 0) {
//             print_init();
//             spinlock_init(&sum_lock, "sum_lock");
//             printf("cpu %d is booting!\n", cpuid);        
//             __sync_synchronize();
//             started = 1;
//              spinlock_acquire(&sum_lock);
//             for(int i = 0; i < 1000000; i++)
//             {
//                 sum++;
//             }
//             spinlock_release(&sum_lock);
//             printf("cpu %d report: sum = %d\n", cpuid, sum);
//         } else {
//             while(started == 0);
//             __sync_synchronize();
//             printf("cpu %d is booting!\n", cpuid);
//             spinlock_acquire(&sum_lock);
//             for(int i = 0; i < 1000000; i++)
//             {  
//                 sum++;
//             }
//              spinlock_release(&sum_lock);
//             printf("cpu %d report: sum = %d\n", cpuid, sum);
//         }   
//         while (1);    
// }

volatile static int started = 0;

volatile static int over_1 = 0, over_2 = 0;

static int* mem[1024];

int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {

        print_init();
        pmem_init();

        printf("cpu %d is booting!\n", cpuid);
        __sync_synchronize();
        started = 1;

        for(int i = 0; i < 512; i++) {
            mem[i] = pmem_alloc(true);
            memset(mem[i], 1, PGSIZE);
            printf("mem = %p, data = %d\n", mem[i], mem[i][0]);
        }
        printf("cpu %d alloc over\n", cpuid);
        over_1 = 1;
        
        while(over_1 == 0 || over_2 == 0);
        
        for(int i = 0; i < 512; i++)
            pmem_free((uint64)mem[i], true);
        printf("cpu %d free over\n", cpuid);

    } else {

        while(started == 0);
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
        
        for(int i = 512; i < 1024; i++) {
            mem[i] = pmem_alloc(true);
            memset(mem[i], 1, PGSIZE);
            printf("mem = %p, data = %d\n", mem[i], mem[i][0]);
        }
        printf("cpu %d alloc over\n", cpuid);
        over_2 = 1;

        while(over_1 == 0 || over_2 == 0);

        for(int i = 512; i < 1024; i++)
            pmem_free((uint64)mem[i], true);
        printf("cpu %d free over\n", cpuid);        
 
    }
    while (1);    
}