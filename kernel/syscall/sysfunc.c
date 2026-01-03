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
#include "fs/elf.h"
#include "fs/inode.h"
#include "fs/dir.h"
#include "fs/fs.h"
#include "memlayout.h"

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
    // printf("sys_exit called\n");
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

static int elf_flags_to_perm(uint32 flags)
{
    int perm = PTE_U;
    if (flags & ELF_PROG_FLAG_READ) perm |= PTE_R;
    if (flags & ELF_PROG_FLAG_WRITE) perm |= PTE_W;
    if (flags & ELF_PROG_FLAG_EXEC) perm |= PTE_X;
    return perm;
}

static int map_and_zero(pgtbl_t pt, uint64 va_start, uint64 va_end, int perm)
{
    uint64 a = PG_ROUND_DOWN(va_start);
    uint64 last = PG_ROUND_UP(va_end);

    for (; a < last; a += PAGESIZE) {
        void *kva = pmem_alloc(false);
        if (!kva) return -1;
        memset(kva, 0, PAGESIZE);

        uint64 pa = kva2pa(kva);
        vm_mappages(pt, a, pa, PAGESIZE, perm);
    }
    return 0;
}

// 把一个 PT_LOAD 段装载到 newpt
// 返回 0 成功，-1 失败
static int load_segment(pgtbl_t newpt, inode_t *ip, proghdr_t *ph)
{
    // 1) 基本合法性检查（按 xv6 的“简单实现”要求 vaddr 页对齐）
    if (ph->type != ELF_PROG_LOAD) return 0;      // 不是 load 段就忽略
    if (ph->memsz < ph->filesz) return -1;
    if (ph->vaddr % PAGESIZE != 0) return -1;

    int perm = elf_flags_to_perm(ph->flags);

    // 2) 先把 [vaddr, vaddr+memsz) 映射出来并清零
    //    这样 BSS（filesz..memsz）天然就是 0
    if (map_and_zero(newpt, ph->vaddr, ph->vaddr + ph->memsz, perm) < 0)
        return -1;

    // 3) 再把文件里 [off, off+filesz) 读出来写进用户 VA
    //    用 1KB 缓冲按块搬运，匹配你 BLOCK_SIZE=1024
    uint64 off  = ph->off;
    uint64 va   = ph->vaddr;
    uint64 left = ph->filesz;

    char buf[BLOCK_SIZE];

    while (left > 0) {
        uint32 n = (left > BLOCK_SIZE) ? BLOCK_SIZE : (uint32)left;

        // 从 inode 读到内核 buf（你的 inode_read_data 语义：读文件 -> dst，user_dst=false）
        uint32 r = inode_read_data(ip, (uint32)off, n, (uint64)buf, false);
        if (r != n) return -1;

        // 写到 newpt 的用户空间
        // 你的 uvm_copyout 是 void：这里假设它内部保证 va 可写，否则 panic
        uvm_copyout(newpt, va, (uint64)buf, n);

        off  += n;
        va   += n;
        left -= n;
    }

    return 0;
}


int exec(char *path, uint64 uargv)
{
    

    proc_t *p = myproc();
      // ---------- A) 先打开 ELF ----------
    inode_t *ip = path_to_inode(path);
    if(ip == 0)
    {
        printf("exec: path_to_inode failed\n");
        return -1;
    }

    inode_lock(ip);
    struct elfhdr elf;
    if(inode_read_data(ip, 0, sizeof(elf), (uint64)&elf, false)!= sizeof(elf))
    {
        inode_unlock_free(ip);
        printf("exec: read elfhdr failed\n");
        return -1;
    }
    if(elf.magic != ELF_MAGIC)
    {
        inode_unlock_free(ip);
        printf("exec: invalid elf magic\n");
        return -1;
    }
    // ---------- B) 创建新用户页表 ----------
    pgtbl_t new_pgtbl = proc_pgtbl_init((uint64)p->tf);
    if (!new_pgtbl) {
        inode_unlock_free(ip);
        return -1;
    }
    // ---------- C) 读取 program header 并装载段 ----------
    uint64 maxva = 0;
    for(int i = 0; i < elf.phnum;i++)
    {
        struct proghdr ph;
        uint64 pho = elf.phoff + (uint64)i * sizeof(proghdr_t);

        if (inode_read_data(ip, (uint32)pho, sizeof(ph), (uint64)&ph, false) != sizeof(ph)) {
            inode_unlock_free(ip);
            uvm_destroy_pgtbl(new_pgtbl);        
            printf("exec: read proghdr failed\n");
            return -1;
        }

        if (ph.type != ELF_PROG_LOAD) continue;


        if (load_segment(new_pgtbl, ip, &ph) < 0) {
            inode_unlock_free(ip);
            uvm_destroy_pgtbl(new_pgtbl);       
            printf("exec: load_segment failed\n");
            return -1;
        }

         uint64 end = ph.vaddr + ph.memsz;
        if (end > maxva) maxva = end;
    }
    
     inode_unlock_free(ip); // 已经把程序读完了

    // 程序末尾对齐（作为 heap 起点）
    uint64 newsz = PG_ROUND_UP(maxva);

    // ---------- D) 建用户栈（放在 TRAPFRAME 下方） ----------
    uint64 USTACK_TOP    = TRAPFRAME;
    uint64 STACK_PAGE_VA = USTACK_TOP - PAGESIZE;
    uint64 GUARD_VA      = USTACK_TOP - 2*PAGESIZE;

    // guard page：最简单就是“不映射”，留空洞即可（访问会 page fault）
    // stack page：映射一页 RWU
    {
        void *stk_kva = pmem_alloc(false);
        if (!stk_kva) {
            uvm_destroy_pgtbl(new_pgtbl);       
            printf("exec: pmem_alloc for stack failed\n");
            return -1;
        }
        memset(stk_kva, 0, PAGESIZE);
        uint64 stk_pa = kva2pa(stk_kva);
        vm_mappages(new_pgtbl, STACK_PAGE_VA, stk_pa, PAGESIZE, PTE_R | PTE_W | PTE_U);
    }


     // ---------- E) 从旧地址空间取 argv，把参数压到新栈 ----------
    // 1) 先把用户 argv[i] 指针数组拷到内核里（避免换页表后读不到）

    uint64 kargv_ptrs[ELF_MAXARGS];
    int argc = 0;

    for (; argc < ELF_MAXARGS; argc++)
    {
        uint64 uptr = 0;
        uint64 va0 = PG_ROUND_DOWN((uint64)uargv);
        pte_t *pte = vm_getpte(p->pgtbl, va0, false);
      
        uvm_copyin(p->pgtbl, (uint64)&uptr, uargv + (uint64)argc * sizeof(uint64), sizeof(uint64));
        if (uptr == 0) { // argv 以 NULL 结尾
            break;
        }
        kargv_ptrs[argc] = uptr;
    }
    // 2) 把每个参数字符串拷到内核缓冲，再 copyout 到新栈
    uint64 sp = USTACK_TOP;
    uint64 ustack_argv[ELF_MAXARGS + 1];

    for(int i = 0;i < argc;i++)
    {
        char s[128]; // 简化：限制单个参数长度;
        memset(s, 0, sizeof(s));
        // 从旧页表把字符串拷到内核
        if (uvm_copyin_str(p->pgtbl, s, kargv_ptrs[i], sizeof(s)) < 0) {
            uvm_destroy_pgtbl(new_pgtbl); 
            printf("exec: uvm_copyin_str failed\n");
            return -1;
        }
        uint64 slen = kstrlen(s) + 1;
        sp -= slen;
        sp &= ~0xFULL; // 16B 对齐（riscv ABI）
        if (sp < GUARD_VA + PAGESIZE) { // 栈溢出到 guard 区
            uvm_destroy_pgtbl(new_pgtbl); 
            printf("exec: kstrlen failed\n");
            return -1;
        }
        // 写入新页表的用户栈
        uvm_copyout(new_pgtbl, sp, (uint64)s, (uint32)slen);
        ustack_argv[i] = sp;
    }

    ustack_argv[argc] = 0;
     // 3) 再把 argv 指针数组压栈
    sp -= (uint64)(argc + 1) * sizeof(uint64);
    sp &= ~0xFULL;
    if (sp < GUARD_VA + PAGESIZE) {
        uvm_destroy_pgtbl(new_pgtbl); 
        printf("exec:argv 指针数组压栈\n");
        return -1;
    }
    uvm_copyout(new_pgtbl, sp, (uint64)ustack_argv, (uint32)((argc + 1) * sizeof(uint64)));

    uint64 user_argv_va = sp;


    // ---------- F) Commit：替换进程地址空间 ----------
    pgtbl_t oldpt = p->pgtbl;

    p->pgtbl = new_pgtbl;
    p->heap_top = newsz;     // 你的 brk/heap 逻辑用 heap_top
    // 如果你维护用户栈页数：
    p->ustack_pages = 1;

    // 设置用户入口与栈
    p->tf->epc = elf.entry;
    p->tf->sp  = sp;

    // 让用户 main(argc, argv) 能拿到参数：约定 a0=argc, a1=argv
    p->tf->a0  = argc;
    p->tf->a1  = user_argv_va;

    // 释放旧页表
    uvm_destroy_pgtbl(oldpt);   // <<< 你替换成你自己的释放函数

    return argc;
}

// 执行一个ELF文件
// char* path
// char** argv
// 成功返回argc 失败返回-1
uint64 sys_exec()
{
    char path[DIR_PATH_LEN];    // 文件路径
    char* argv[ELF_MAXARGS];    // 参数指针数组
    uint64 uargv;  // 用户态 argv(char**) 的地址
    // 1) 取 path 字符串（从用户态拷到内核 path[]）
    arg_str(0, path, sizeof(path));
    // 2) 取 argv 指针本身（用户态地址，不要在这里展开）
    arg_uint64(1, &uargv);

    // 3) 调真正的 exec：注意你的 exec 应该是 exec(path, uargv)
    int ret = exec(path, uargv);
    if (ret < 0) return (uint64)-1;
    return (uint64)ret;
}