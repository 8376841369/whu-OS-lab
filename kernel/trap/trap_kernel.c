#include "lib/print.h"
#include "dev/timer.h"
#include "dev/uart.h"
#include "dev/plic.h"
#include "trap/trap.h"
#include "proc/proc.h"
#include "dev/plic.h"
#include "memlayout.h"
#include "riscv.h"
#include "proc/cpu.h"
#include "dev/vio.h"

// 中断信息
static char* interrupt_info[16] = {
    "U-mode software interrupt",      // 0
    "S-mode software interrupt",      // 1
    "reserved-1",                     // 2
    "M-mode software interrupt",      // 3
    "U-mode timer interrupt",         // 4
    "S-mode timer interrupt",         // 5
    "reserved-2",                     // 6
    "M-mode timer interrupt",         // 7
    "U-mode external interrupt",      // 8
    "S-mode external interrupt",      // 9
    "reserved-3",                     // 10
    "M-mode external interrupt",      // 11
    "reserved-4",                     // 12
    "reserved-5",                     // 13
    "reserved-6",                     // 14
    "reserved-7",                     // 15
};

// 异常信息
static char* exception_info[16] = {
    "Instruction address misaligned", // 0
    "Instruction access fault",       // 1
    "Illegal instruction",            // 2
    "Breakpoint",                     // 3
    "Load address misaligned",        // 4
    "Load access fault",              // 5
    "Store/AMO address misaligned",   // 6
    "Store/AMO access fault",         // 7
    "Environment call from U-mode",   // 8
    "Environment call from S-mode",   // 9
    "reserved-1",                     // 10
    "Environment call from M-mode",   // 11
    "Instruction page fault",         // 12
    "Load page fault",                // 13
    "reserved-2",                     // 14
    "Store/AMO page fault",           // 15
};

// in trap.S
// 内核中断处理流程
extern void kernel_vector();

// 初始化trap中全局共享的东西
void trap_kernel_init()
{
    plic_init();
    w_sip(0);
}

// 各个核心trap初始化
void trap_kernel_inithart()
{
    timer_create();
    w_stvec((uint64)kernel_vector);
    plic_inithart();
}

// 外设中断处理 (基于PLIC)
void external_interrupt_handler()
{
    int hart = mycpuid();
    int irq = plic_claim();//领取中断号
    
    switch (irq)
    {
    case 0:                 // 无中断
        break;
    case UART_IRQ:             // 串口中断（键盘输入）
        //printf("kernel external interrupt\n");
        uart_intr();
        break;
    case VIRTIO_IRQ:        // ★ 磁盘中断
        printf("kernel virtio disk interrupt\n");
        virtio_disk_intr(); // ★ 必须：完成请求、清 b->disk、wakeup(b)、回收desc
    default:
        printf("unexpected PLIC irq=%d on hart=%d\n", irq, hart);
        break;
    }
    if (irq)
    plic_complete(irq);//完成中断处理
}

// 时钟中断处理 (基于CLINT)
void timer_interrupt_handler()
{
    if(mycpuid()==0)
    {
         timer_update();
         printf("time %d\n", timer_get_ticks());
    }
    // 设置下一个时钟中断时间
    w_stimecmp(r_time() + INTERVAL);
}

// 在kernel_vector()里面调用
// 内核态trap处理的核心逻辑
void trap_kernel_handler()
{
    uint64 sepc = r_sepc();          // 记录了发生异常时的pc值
    uint64 sstatus = r_sstatus();    // 与特权模式和中断相关的状态信息
    uint64 scause = r_scause();      // 引发trap的原因
    uint64 stval = r_stval();        // 发生trap时保存的附加信息(不同trap不一样)

    // 确认trap来自S-mode且此时trap处于关闭状态
    assert(sstatus & SSTATUS_SPP, "trap_kernel_handler: not from s-mode");
    assert(intr_get() == 0, "trap_kernel_handler: interreput enabled");

    int trap_id = scause & 0xf; 
    int is_interrupt = (scause >> 63) & 1;
    int need_yield = 0; 
    // 中断异常处理核心逻辑
    if(is_interrupt)
    {
        switch (trap_id)
        {
        case 5:
            timer_interrupt_handler();    // 里面会续期: stimecmp = time + INTERVAL
            if(myproc()!=0&&myproc()->state==RUNNING)
                need_yield = 1;
            break;
        case 9:
            
            external_interrupt_handler();
            break;
        
        default:
            break;
        }
    }
    if(need_yield)
    {
        proc_yield();
    }
    w_sepc(sepc);
    w_sstatus(sstatus);
}