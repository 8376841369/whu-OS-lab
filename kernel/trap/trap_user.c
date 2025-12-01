#include "lib/print.h"
#include "trap/trap.h"
#include "proc/cpu.h"
#include "mem/vmem.h"
#include "memlayout.h"
#include "riscv.h"
#include "syscall_h/syscall.h"
extern pgtbl_t kernel_pgtbl;

// in trampoline.S
extern char trampoline[];      // 内核和用户切换的代码
extern char user_vector[];     // 用户触发trap进入内核
extern char user_return[];     // trap处理完毕返回用户

// in trap.S
extern char kernel_vector[];   // 内核态trap处理流程

// in trap_kernel.c
extern char* interrupt_info[16]; // 中断错误信息
extern char* exception_info[16]; // 异常错误信息

// 在 user_vector() 里面调用
// 用户态 trap 处理的核心逻辑
void trap_user_handler()
{
    // 先切到内核的 trap 向量
    w_stvec((uint64)kernel_vector);

    uint64 sepc    = r_sepc();       // 发生异常时的 pc
    uint64 sstatus = r_sstatus();    // 特权模式和中断相关状态
    uint64 scause  = r_scause();     // trap 原因
    uint64 stval   = r_stval();      // trap 附加信息
    proc_t *p      = myproc();

    // 确认 trap 来自 U-mode
    assert((sstatus & SSTATUS_SPP) == 0, "trap_user_handler: not from u-mode");

    int trap_id      = scause & 0xf;
    int is_interrupt = (scause >> 63) & 1;

    if (is_interrupt)
    {
        switch (trap_id)
        {
        case 5:
            // S-mode timer interrupt
            timer_interrupt_handler();  // 里面续期 stimecmp = time + INTERVAL

            // 仿照 xv6：如果当前有 RUNNING 的进程，就让出 CPU
            if (p != 0 && p->state == RUNNING) {
                proc_yield();
            }
            break;

        case 9:
            // 外部中断（PLIC）
            external_interrupt_handler();
            break;

        default:
            // 其它中断先简单打个 log
            // printf("user interrupt: scause=%lx stval=%lx sepc=%lx\n", scause, stval, sepc);
            break;
        }
    }
    else
    {
        // 同步异常（不含 syscall）
        switch (trap_id)
        {
        case 8: // syscall
            // 先更新 epc，防止重复执行 ecall 指令
            p->tf->epc = sepc + 4;

            intr_on();   // 允许在 syscall 中被中断
            syscall();   // 处理系统调用
            break;

        default:
            // 这里可以选择 kill 进程 / 打 log 等
            // printf("user exception: scause=%lx stval=%lx sepc=%lx\n", scause, stval, sepc);
            // proc_exit(-1); // 若你有类似接口
            break;
        }
    }

    // 统一的回用户态收尾：
    //   - stvec 切回 user_vector（高地址别名）
    //   - 清 SPP=0，置 SPIE=1
    //   - 跳 trampoline.user_return(TRAPFRAME, satp) -> sret
    trap_user_return();
}

// 调用user_return()
// 内核态返回用户态
void trap_user_return()
{
    
    proc_t *p = myproc();
   intr_off();

    p->tf->kernel_satp = MAKE_SATP(kernel_pgtbl);//内核页表
    p->tf->kernel_hartid = r_tp();
    p->tf->kernel_sp = p->kstack+PAGESIZE; // 内核栈顶
    p->tf->kernel_trap = (uint64)trap_user_handler;

    volatile int64 fn = (uint64)TRAMPOLINE + ((uint64)user_return - (uint64)trampoline);
    
    w_stvec((uint64)TRAMPOLINE + ((uint64)user_vector - (uint64)trampoline));
     //中断相关寄存器设置
    uint64 x = r_sstatus();
    x &= ~SSTATUS_SPP;
    x |=  SSTATUS_SPIE;
    w_sstatus(x);
    
    w_sepc(p->tf->epc);  // 不是必须，但一致性OK
   // printf("trap_user_return:epc=0x%lx\n",  p->tf->epc);
   
   ((void (*)(uint64,uint64))fn)((uint64)TRAPFRAME, MAKE_SATP(p->pgtbl));//调用了user_return
    panic("trap_user_return unreachable");
}