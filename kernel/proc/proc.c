#include "lib/print.h"
#include "lib/string.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "proc/initcode.h"
#include "memlayout.h"
#include "riscv.h"

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t* old, context_t* new);

// in trap_user.c
extern void trap_user_return();
extern void trap_user_handler();
//in vmem.c
extern pgtbl_t kernel_pgtbl;



// 第一个进程
static proc_t proczero;


static void forkret(void)
{
    extern char user_return[];
    proc_t *p = myproc();
    int64 fn = (uint64)TRAMPOLINE + ((uint64)user_return - (uint64)trampoline);

     //中断相关寄存器设置
    uint64 x = r_sstatus();
    x &= ~SSTATUS_SPP;
    x |=  SSTATUS_SPIE;
    w_sstatus(x);
    w_sepc(p->tf->epc);  // 不是必须，但一致性OK

    ((void (*) (uint64,uint64)) fn) ((uint64)p->tf, MAKE_SATP(p->pgtbl));
    panic("forkret unreachable");

}

// 获得一个初始化过的用户页表
// 完成了trapframe 和 trampoline 的映射
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    pgtbl_t upgt = (pgtbl_t)pmem_alloc(true);//给用户页表分配一页内存
    if(!upgt)
    {
        panic("proc_pgtbl_init: pmem_alloc failed");
    }
    memset(upgt,0,PAGESIZE);
    trapframe = PG_ROUND_DOWN(trapframe);
    //TRAMFRAME是一个虚拟地址，映射到每一个进程的trapframe的物理地址
    vm_mappages(upgt,(uint64)TRAPFRAME,trapframe,PGSIZE,PTE_R | PTE_W);
    //trampoline 映射
    uint64 tramp_pa = kva2pa((void*)trampoline);
    vm_mappages(upgt,(uint64)TRAMPOLINE,tramp_pa,PGSIZE,PTE_X | PTE_R);

    return upgt;
}


/*
    第一个用户态进程的创建
    它的代码和数据位于initcode.h的initcode数组

    第一个进程的用户地址空间布局:
    trapoline   (1 page)
    trapframe   (1 page)
    ustack      (1 page)
    .......
                        <--heap_top
    code + data (1 page)
    empty space (1 page) 最低的4096字节 不分配物理页，同时不可访问
*/
void proc_make_fisrt()
{   
    intr_off();
    uint64 page;//data+code
    proc_t* p = &proczero;
    memset(p,0,sizeof(*p));
    
    // pid 设置
    p->pid = 1;
    void * tf_kva =  pmem_alloc(true);
    p->tf = (trapframe_t*)tf_kva;
    if(!tf_kva)
    {
        panic("proc_make_first: pmem_alloc for trapframe failed");
    }
    memset(tf_kva,0,PAGESIZE);
    uint64 tf_pa = kva2pa(tf_kva);
    // pagetable 初始化
    pgtbl_t upgt = proc_pgtbl_init(tf_pa);
    p->pgtbl = upgt;
    // ustack 映射 + 设置 ustack_pages 
    void * ustack_kva = pmem_alloc(false);
    if(!ustack_kva)
    {
        panic("proc_make_first: pmem_alloc for ustack failed");
    }
    memset(ustack_kva,0,PAGESIZE);
    int64 ustack_pa = kva2pa(ustack_kva);

    int64 USTACK_TOP = TRAPFRAME;
    int64 USTACK_BOTTOM = USTACK_TOP - PAGESIZE;
    vm_mappages(upgt,USTACK_BOTTOM,ustack_pa,PAGESIZE,PTE_R | PTE_W | PTE_U);
    p->ustack_pages = 1;
    // data + code 映射
    if(initcode_len>PAGESIZE) panic("proc_make_first: initcode too large");
    page = (uint64)pmem_alloc(false);
    if(!page)
    {
        panic("proc_make_first: pmem_alloc for code+data failed");
    }
    memset((void*)page,0,PAGESIZE);
    memcpy((void*)page,initcode,initcode_len);
    uint64 code_pa = kva2pa((void*)page);
  
    // 代码虚拟地址放在 0x1000（第一页空洞后）
     uint64 CODE_VA = PAGESIZE;
     vm_mappages(p->pgtbl,CODE_VA,code_pa,PAGESIZE,PTE_R | PTE_W | PTE_X | PTE_U);
    // 设置 heap_top,
    p->heap_top = CODE_VA + PAGESIZE; // 紧接着代码段后面
    // tf字段设置
    p->tf->epc = CODE_VA; // 从代码段开始执行
    p->tf->sp = USTACK_TOP; // 用户栈顶
    p->tf->kernel_satp = MAKE_SATP(kernel_pgtbl);//内核页表
    p->tf->kernel_hartid = r_tp();
    p->tf->kernel_sp = 0; // 内核栈顶
    p->tf->kernel_trap = (uint64)trap_user_handler;

    // 内核字段设置
    void * kstack_kva = pmem_alloc(true);
    if(!kstack_kva)
    {
        panic("proc_make_first: pmem_alloc for kstack failed");
    }
    memset(kstack_kva,0,PAGESIZE);
    p->kstack = ((uint64)kstack_kva + PGSIZE) & ~0xFULL; // 栈向下生长，16B对齐
    p->tf->kernel_sp = p->kstack;// 设置内核栈顶

   

    // 上下文切换

    struct cpu *c = mycpu();
    c->proc = p;

    memset(&p->ctx,0,sizeof(p->ctx));
    p->ctx.ra = (uint64)trap_user_return;
    p->ctx.sp = p->kstack;  
   //dummy switch
    
    extern char user_vector[];
    w_stvec((uint64)TRAMPOLINE + ((uint64)user_vector - (uint64)trampoline));
    context_t dummy = {0};
    swtch(&dummy, &p->ctx);

    panic("unexpected return from swtch");
}