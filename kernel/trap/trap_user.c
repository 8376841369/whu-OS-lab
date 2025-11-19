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

// 在user_vector()里面调用
// 用户态trap处理的核心逻辑
void trap_user_handler()
{
    w_stvec((uint64)kernel_vector);

    uint64 sepc = r_sepc();          // 记录了发生异常时的pc值
    uint64 sstatus = r_sstatus();    // 与特权模式和中断相关的状态信息
    uint64 scause = r_scause();      // 引发trap的原因
    uint64 stval = r_stval();        // 发生trap时保存的附加信息(不同trap不一样)
    proc_t* p = myproc();
     
    // 确认trap来自U-mode
    assert((sstatus & SSTATUS_SPP) == 0, "trap_user_handler: not from u-mode");
    int trap_id = scause & 0xf; 
    int is_interrupt = (scause >> 63) & 1;
   
    // printf("trap from user mode: cause=%d  is_interrupt=%d stval=0x%lx sepc=0x%lx cpu=%d\n",
    //        scause,
    //        is_interrupt,
    //        stval,
    //        sepc,
    //        r_tp()
    //        );

    // 中断异常处理核心逻辑
    if(is_interrupt)
    {
        switch (trap_id)
        {
        case 5:
            timer_interrupt_handler();    // 里面会续期: stimecmp = time + INTERVAL
            goto RETURN_TO_USER;
            
        case 9:
            external_interrupt_handler();
            goto RETURN_TO_USER;
            
        default:
           goto RETURN_TO_USER;
        }
    }

    // 其他异常处理
    switch (trap_id)
    {
    case 8:// syscall
        // 先更新pc，防止重复执行syscall指令
        p->tf->epc = sepc + 4;
        intr_on(); // 允许中断
        syscall();
      
        goto RETURN_TO_USER;
       
    
    default:
        goto RETURN_TO_USER;
          
    }
RETURN_TO_USER:
    // 统一的回用户态收尾：调用 trap_user_return() 完成：
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