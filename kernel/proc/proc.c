#include "lib/print.h"
#include "lib/string.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "proc/initcode.h"
#include "memlayout.h"
#include "riscv.h"
#include "proc/proc.h"

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t* old, context_t* new);

// in trap_user.c
extern void trap_user_return();
extern void trap_user_handler();
//in vmem.c
extern pgtbl_t kernel_pgtbl;


/*----------------本地变量------------------*/

// 进程数组
#define NPROC 64
static proc_t procs[NPROC];


// 第一个进程的指针
//static proc_t* proczero;

// 全局的pid和保护它的锁 
static int global_pid = 1;
static spinlock_t lk_pid;

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;


// 申请一个pid(锁保护)
static int alloc_pid()
{
    int tmp = 0;
    spinlock_acquire(&lk_pid);
    assert(global_pid >= 0, "alloc_pid: overflow");
    tmp = global_pid++;
    spinlock_release(&lk_pid);
    return tmp;
}

// 释放锁 + 调用 trap_user_return
static void fork_return()
{
    // 由于调度器中上了锁，所以这里需要解锁
    proc_t* p = myproc();
    spinlock_release(&p->lk);
    trap_user_return();
}

// 返回一个未使用的进程空间
// 设置pid + 设置上下文中的ra和sp
// 申请tf和pgtbl使用的物理页
proc_t* proc_alloc()
{
    proc_t* p;

    for(p = procs;p<&procs[NPROC];p++)
    {
        spinlock_acquire(&p->lk);
        if(p->state == UNUSED)
        {
            goto FOUND;
        }
        else
        {
            spinlock_release(&p->lk);
        }
    }
    return 0;//fail
FOUND:
    p->pid = alloc_pid();
    p->state = RUNNABLE;

    // trapframe 申请
    void * tf_kva = pmem_alloc(true);
    if(!tf_kva)
    {
        proc_free(p);
        spinlock_release(&p->lk);
        return 0;
    }
    memset(tf_kva,0,PAGESIZE);
    p->tf = (trapframe_t*)tf_kva;

    // pgtbl 申请
    pgtbl_t upgt = proc_pgtbl_init(kva2pa(tf_kva));
    p->pgtbl = upgt;
    // 上下文设置
    memset(&p->ctx,0,sizeof(p->ctx));
    p->ctx.ra = (uint64)fork_return;
    p->ctx.sp = p->kstack; // 内核栈顶

    
    return p;
}

// 释放一个进程空间
// 释放pgtbl的整个地址空间
// 释放mmap_region到仓库
// 设置其余各个字段为合适初始值
// tips: 调用者需持有p->lk
void proc_free(proc_t* p)
{
    if(p->tf)
    {
        pmem_free((uint64)(p->tf), true);
        p->tf = 0;
    }
    if(p->pgtbl)
    {
        vm_unmappages(p->pgtbl,0,VA_MAX,true);
        pmem_free((uint64)(p->pgtbl), true);
        p->pgtbl = 0;
    }
    p->pid = 0;
    p->parent = 0;
    p->exit_state = 0;
    p->sleep_space = 0;
    p->heap_top     = 0;
    p->ustack_pages = 0;
    
   
    memset(&p->ctx, 0, sizeof(p->ctx));
    // 如果你希望更稳妥，也可以把 kstack 置 0（前提是别再用到它）
    // p->kstack = 0;
    p->state = UNUSED;
}

// 进程模块初始化
void proc_init()
{
     struct proc *p;
  
    spinlock_init(&lk_pid, "nextpid");
    spinlock_init(&wait_lock, "wait_lock");
    for(p = procs; p < &procs[NPROC]; p++) {
      spinlock_init(&p->lk, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - procs));
  }
}



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
    vm_mappages(upgt,(uint64)TRAPFRAME,trapframe,PGSIZE,PTE_R | PTE_W|PTE_V);
    //trampoline 映射
    uint64 tramp_pa = kva2pa((void*)trampoline);
    vm_mappages(upgt,(uint64)TRAMPOLINE,tramp_pa,PGSIZE,PTE_X | PTE_R|PTE_V);

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
void proc_make_first()
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

// 进程复制
// UNUSED -> RUNNABLE
int proc_fork()
{
    int i, pid;
    struct proc *np;
    struct proc *p = myproc();

    if((np = proc_alloc()) == 0)
    {
        return -1;
    }
  
    if(uvmcopy(p->pgtbl,np->pgtbl,p->heap_top , p->ustack_pages )<0)
    {
        proc_free(np);
        spinlock_release(&np->lk);
        return -1;
    }
    *(np->tf) =*(p->tf);//复制trapframe
    np->tf->a0 = 0;//子进程返回值为0
    pid = np->pid;
    spinlock_release(&np->lk);

    spinlock_acquire(&wait_lock);
    np->parent = p;
    spinlock_release(&wait_lock);
    spinlock_acquire(&np->lk);
    np->state = RUNNABLE;
    spinlock_release(&np->lk);
    return pid;
}

// 进程放弃CPU的控制权
// RUNNING -> RUNNABLE
void proc_yield()
{

}

// 等待一个子进程进入 ZOMBIE 状态
// 将退出的子进程的exit_state放入用户给的地址 addr
// 成功返回子进程pid，失败返回-1
int proc_wait(uint64 addr)
{

}

// 父进程退出，子进程认proczero做父，因为它永不退出
static void proc_reparent(proc_t* parent)
{

}

// 唤醒一个进程
static void proc_wakeup_one(proc_t* p)
{
    assert(spinlock_holding(&p->lk), "proc_wakeup_one: lock");
    if(p->state == SLEEPING && p->sleep_space == p) {
        p->state = RUNNABLE;
    }
}

// 进程退出
void proc_exit(int exit_state)
{

}

// 进程切换到调度器
// ps: 调用者保证持有当前进程的锁
void proc_sched()
{

}

// 调度器
void proc_scheduler()
{

}

// 进程睡眠在sleep_space
void proc_sleep(void* sleep_space, spinlock_t* lk)
{

}

// 唤醒所有在sleep_space沉睡的进程
void proc_wakeup(void* sleep_space)
{

}