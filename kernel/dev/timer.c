#include "lib/lock.h"
#include "lib/print.h"
#include "dev/timer.h"
#include "memlayout.h"
#include "riscv.h"

/*-------------------- 工作在M-mode --------------------*/

// in trap.S M-mode时钟中断处理流程()
extern void timer_vector();

// 每个CPU在时钟中断中需要的临时空间(考虑为什么可以这么写)
static uint64 mscratch[NCPU][5];

// 时钟初始化
// called in start.c
void timer_init()
{
    w_mie(r_mie() | MIE_STIE); // 允许时钟中断
    w_menvcfg(r_menvcfg() | (1L<<63)); // 允许机器模式下的定时器中断
    w_mcounteren(r_mcounteren() | 2); 
    w_stimecmp(r_time() + INTERVAL);
}//借鉴自 xv6-riscv


/*--------------------- 工作在S-mode --------------------*/

// 系统时钟
static timer_t sys_timer;

// 时钟创建(初始化系统时钟)
void timer_create()
{

}

// 时钟更新(ticks++ with lock)
void timer_update()
{

}

// 返回系统时钟ticks
uint64 timer_get_ticks()
{

}