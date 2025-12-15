#include "riscv.h"
#include "lib/print.h"
#include "proc/proc.h"
#include "dev/uart.h"
#include "lib/lock.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "lib/string.h"
#include "trap/trap.h"
#include "dev/timer.h"
#include "proc/cpu.h"
#include "dev/plic.h"
#include "fs/fs.h"
#include "dev/vio.h"

volatile static int started = 0;

int main(void)
{
    int id = mycpuid();   // 或者用 cpuid()/mycpuid()

    if (id == 0) {
        // 基础初始化
        pmem_init();
        kvm_init();
        kvm_inithart();
        trap_kernel_init();
        print_init();
        uart_init();
        proc_init();
        // 安装 S-mode trap 入口（全局一次）

        // 本核的中断/PLIC/定时器（每核）
        trap_kernel_inithart();   // 内部应打开 SIE_STIE + SSTATUS_SIE

       
        printf("cpu %d is booting! Sstc timer test starts.\n", id);

        __sync_synchronize();
        started = 1;              // 放行其他核

         
       proc_make_first(); 
       virtio_disk_init();  
       proc_scheduler(); 

        // 心跳观测循环：每 10 tick 打印一次
        uint64 last = timer_get_ticks();
        while (1) {
            uint64 t = timer_get_ticks();
            if (t != last) {
                if ((t % 10) == 0) {     // 假设 INTERVAL=0.1s → 约 1 秒打印
                    printf("[cpu%d] ticks=%lu\n", id, t);
                }
                last = t;
            }
        }

    } else {
        // 等待 boot 核完成全局初始化
        while (started == 0) { /* spin */ }
        __sync_synchronize();
        kvm_inithart();
        // 每核初始化：打开本核中断 & 预约本核 stimecmp
        trap_kernel_inithart();
        
        printf("cpu %d is booting! Sstc timer armed.\n", id);

        // 次核也跑一个轻量观测（减少刷屏：每 50 tick 打印一次）
        uint64 last = timer_get_ticks();
        while (1) {
            uint64 t = timer_get_ticks();
          // printf("cpu%d tick=%lu\n",id,t);
            if (t != last) {
                if ((t % 50) == 0) {
                    printf("[cpu%d] ticks=%lu\n", id, t);
                }
                last = t;
            }
        }
    }
}