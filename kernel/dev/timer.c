#include "lib/lock.h"
#include "lib/print.h"
#include "proc/proc.h"
#include "dev/timer.h"
#include "memlayout.h"
#include "riscv.h"

/*-------------------- 工作在M-mode --------------------*/

// in trap.S M-mode时钟中断处理流程()
extern void timer_vector();

// 每个CPU在时钟中断中需要的临时空间(考虑为什么可以这么写)
//static uint64 mscratch[NCPU][5];

// 时钟初始化
// called in start.c
void timer_init()
{
    w_menvcfg(r_menvcfg() | (1L<<63)); // 允许S模式下的写入stimecmp CSR
    w_mcounteren(r_mcounteren() | 2); //控制S 态能否读一组计数器 CSR 即time
    w_stimecmp(r_time() + INTERVAL);
}//借鉴自 xv6-riscv


/*--------------------- 工作在S-mode --------------------*/

// 系统时钟
static timer_t sys_timer;

// 时钟创建(初始化系统时钟)
void timer_create()
{
    if(mycpuid()==0)
    {
        sys_timer.ticks = 0;
        spinlock_init(&sys_timer.lk, "timer");
    }
    // 2) 打开 S 态中断：
    //    - sstatus.SIE：S 态全局中断开关
    w_sstatus(r_sstatus()| SSTATUS_SIE);//允许S态中断
    // 设置下一个时钟中断时间
    w_stimecmp(r_time() + INTERVAL);
}

// 时钟更新(ticks++ with lock)
//每一次时钟中断都会调用这个函数
void timer_update()
{
    spinlock_acquire(&sys_timer.lk);
    sys_timer.ticks++;
    spinlock_release(&sys_timer.lk);
}

// 返回系统时钟ticks
uint64 timer_get_ticks()
{
    uint64 t;
    spinlock_acquire(&sys_timer.lk);
    t = sys_timer.ticks;
    spinlock_release(&sys_timer.lk);
    return t;
}