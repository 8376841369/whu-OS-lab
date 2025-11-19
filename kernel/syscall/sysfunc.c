#include "proc/cpu.h"
#include "mem/vmem.h"
#include "mem/pmem.h"
#include "mem/mmap.h"
#include "lib/string.h"
#include "lib/print.h"
#include "syscall_h/sysfunc.h"
#include "syscall_h/syscall.h"
#include "syscall_h/sysnum.h"
#include "riscv.h"
#include "dev/timer.h"

// 堆伸缩
// uint64 new_heap_top 新的堆顶 (如果是0代表查询, 返回旧的堆顶)
// 成功返回新的堆顶 失败返回-1
uint64 sys_brk()
{
    printf("sys_brk called\n");
    proc_t *p = myproc();
    uint64 new_heap_top;
    arg_uint64(0, &new_heap_top);// 获取参数
    uint64 old_heap_top = p->heap_top;// 保存旧堆顶

    if(new_heap_top <= 0) return old_heap_top;//参数错误的情况，堆顶不变

    if(new_heap_top > p->ctx.sp)//堆顶不能超过栈顶
    {
        printf("sys_brk: new_heap_top exceeds stack top\n");
        return -1;
    }

    
    if(new_heap_top > old_heap_top)
    {
        int len = new_heap_top - old_heap_top;
        len = PG_ROUND_UP(len);
        uint64 ret = uvm_heap_grow(p->pgtbl, old_heap_top, len); 
        if(ret == old_heap_top)//增长失败
        {
            return -1;
        }  
        p->heap_top = ret;//更新堆顶
        printf("sys_brk: heap grow from 0x%lx to 0x%lx\n", old_heap_top, ret);
        return ret;
    }

    if(new_heap_top < old_heap_top)
    {
        int len = old_heap_top - new_heap_top;
        len = PG_ROUND_UP(len);
        uint64 ret = uvm_heap_ungrow(p->pgtbl, old_heap_top, len);    
        if(ret == old_heap_top)//缩小失败
        {
            return -1;
        }
        p->heap_top = ret;//更新堆顶
        printf("sys_brk: heap ungrow from 0x%lx to 0x%lx\n", old_heap_top, ret);
        return ret;
    }
   printf("sys_brk: heap top unchanged at 0x%lx\n", old_heap_top);
   return old_heap_top;//堆顶不变
}

// 内存映射
// uint64 start 起始地址 (如果为0则由内核自主选择一个合适的起点, 通常是顺序扫描找到一个够大的空闲空间)
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回映射空间的起始地址, 失败返回-1
uint64 sys_mmap()
{

}

// 取消内存映射
// uint64 start 起始地址
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回0 失败返回-1
uint64 sys_munmap()
{

}

// copyin 测试 (int 数组)
// uint64 addr
// uint32 len
// 返回 0
uint64 sys_copyin()
{   
   
    proc_t* p = myproc();
    uint64 addr;
    uint32 len;

    arg_uint64(0, &addr);
    arg_uint32(1, &len);

    int tmp;
    for(int i = 0; i < len; i++) {
        uvm_copyin(p->pgtbl, (uint64)&tmp, addr + i * sizeof(int), sizeof(int));
        printf("get a number from user: %d\n", tmp);
    }

    return 0;
}

// copyout 测试 (int 数组)
// uint64 addr
// 返回数组元素数量
uint64 sys_copyout()
{
    int L[5] = {1, 2, 3, 4, 5};
    proc_t* p = myproc();
    uint64 addr;

    arg_uint64(0, &addr);
    uvm_copyout(p->pgtbl, addr, (uint64)L, sizeof(int) * 5);

    return 5;
}

// copyinstr测试
// uint64 addr
// 成功返回0
uint64 sys_copyinstr()
{
    char s[64];

    arg_str(0, s, 64);
    printf("get str from user: %s\n", s);

    return 0;
}


// 打印字符
// uint64 addr
uint64 sys_print()
{
    //printf("sys_print called:");
    uint64 addr;
   
    arg_uint64(0,&addr);
    if(addr< 0)
    {
        return -1;
    }

    char buf[1024];
    if(uvm_copyin_str(myproc()->pgtbl, buf, addr, sizeof(buf)) < 0)
    {
        return -1;
    }
 
    printf("%s", buf);
 
    return kstrlen(buf);
}

// 进程复制
uint64 sys_fork()
{
    //printf("sys_fork called\n");
    return proc_fork();
}

// 进程等待
// uint64 addr  子进程退出时的exit_state需要放到这里 
uint64 sys_wait()
{
   // printf("sys_wait called\n");
    uint64 p;
  arg_uint64(0, &p);
  return proc_wait(p);
}

// 进程退出
// int exit_state
uint64 sys_exit()
{
    printf("sys_exit called\n");
    int n;
    arg_uint32(0, (uint32*)&n);
    proc_exit(n);
    return 0;// not reached
}

extern timer_t sys_timer;

// 进程睡眠一段时间
// uint32 second 睡眠时间
// 成功返回0, 失败返回-1
uint64 sys_sleep()
{

    int n,ticks0;
    arg_uint32(0, (uint32*)&n);
    spinlock_acquire(&sys_timer.lk);
    ticks0 = sys_timer.ticks;
    while (sys_timer.ticks - ticks0 < n) {
        proc_sleep(&sys_timer, &sys_timer.lk);
    }
    spinlock_release(&sys_timer.lk);
    return 0;
}
