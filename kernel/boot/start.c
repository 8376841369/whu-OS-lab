#include "riscv.h"

void main();


__attribute__ ((aligned (16))) uint8 CPU_stack[4096 * NCPU];

void start()
{

    unsigned long x = r_mstatus();//读取mstatus寄存器
    x &= ~MSTATUS_MPP_MASK;//清空mpp字段
    x |= MSTATUS_MPP_S;//将MPP字段设置为 'S'
    w_mstatus(x);//重新写回

    w_mepc((uint64)main);//写入main，帮助跳转到main函数

    //关闭分页
    w_satp(0);

    // // delegate all interrupts and exceptions to supervisor mode.
    w_medeleg(0xffff);
    w_mideleg(0xffff);
    w_sie(r_sie() | SIE_SEIE | SIE_STIE);

    // // configure Physical Memory Protection to give supervisor mode
    // // access to all of physical memory.
    w_pmpaddr0(0x3fffffffffffffull);
    w_pmpcfg0(0xf);

    //获取硬件ID
    int id = r_mhartid();
    //写入tp
    w_tp(id);

    asm volatile("mret");
}