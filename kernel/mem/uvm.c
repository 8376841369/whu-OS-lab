#include "mem/mmap.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "lib/print.h"
#include "lib/string.h"
#include "memlayout.h"
#include "riscv.h"

// 连续虚拟空间的复制(在uvm_copy_pgtbl中使用)
static void copy_range(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end)
{
    uint64 va, pa, page;
    int flags;
    pte_t* pte;

    for(va = begin; va < end; va += PAGESIZE)
    {
        pte = vm_getpte(old, va, false);
        assert(pte != NULL, "uvm_copy_pgtbl: pte == NULL");
        assert((*pte) & PTE_V, "uvm_copy_pgtbl: pte not valid");
        
        pa = (uint64)PTE_TO_PA(*pte);
        flags = (int)PTE_FLAGS(*pte);

        page = (uint64)pmem_alloc(false);
        memmove((char*)page, (const char*)pa, PAGESIZE);
        vm_mappages(new, va, page, PAGESIZE, flags);
    }
}

// 两个 mmap_region 区域合并
// 保留一个 释放一个 不操作 next 指针
// 在uvm_munmap里使用
static void mmap_merge(mmap_region_t* mmap_1, mmap_region_t* mmap_2, bool keep_mmap_1)
{
    // 确保有效和紧临
    assert(mmap_1 != NULL && mmap_2 != NULL, "mmap_merge: NULL");
    assert(mmap_1->begin + mmap_1->npages * PAGESIZE == mmap_2->begin, "mmap_merge: check fail");
    
    // merge
    if(keep_mmap_1) {
        mmap_1->npages += mmap_2->npages;
        mmap_region_free(mmap_2);
    } else {
        mmap_2->begin -= mmap_1->npages * PAGESIZE;
        mmap_2->npages += mmap_1->npages;
        mmap_region_free(mmap_1);
    }
}

// 打印以 mmap 为首的 mmap 链
// for debug
void uvm_show_mmaplist(mmap_region_t* mmap)
{
    mmap_region_t* tmp = mmap;
    printf("\nmmap allocable area:\n");
    if(tmp == NULL)
        printf("NULL\n");
    while(tmp != NULL) {
        printf("allocable region: %p ~ %p\n", tmp->begin, tmp->begin + tmp->npages * PGSIZE);
        tmp = tmp->next;
    }
}

// 递归释放 页表占用的物理页 和 页表管理的物理页
// ps: 顶级页表level = 3, level = 0 说明是页表管理的物理页
static void destroy_pgtbl(pgtbl_t pgtbl, uint32 level)
{

}

// 页表销毁：trapframe 和 trampoline 单独处理
void uvm_destroy_pgtbl(pgtbl_t pgtbl)
{

}

// 拷贝页表 (拷贝并不包括trapframe 和 trampoline)
void uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top, uint32 ustack_pages, mmap_region_t* mmap)
{
    /* step-1: USER_BASE ~ heap_top */

    /* step-2: ustack */

    /* step-3: mmap_region */
}

// 在用户页表和进程mmap链里 新增mmap区域 [begin, begin + npages * PGSIZE)
// 页面权限为perm
void uvm_mmap(uint64 begin, uint32 npages, int perm)
{
    if(npages == 0) return;
    assert(begin % PAGESIZE == 0, "uvm_mmap: begin not aligned");

    int end = begin + npages * PAGESIZE;
    // 修改 mmap 链 (分情况的链式操作)
    
    // 修改页表 (物理页申请 + 页表映射)

}

// 在用户页表和进程mmap链里释放mmap区域 [begin, begin + npages * PGSIZE)
void uvm_munmap(uint64 begin, uint32 npages)
{
    if(npages == 0) return;
    assert(begin % PAGESIZE == 0, "uvm_munmap: begin not aligned");

    uint64 a;
    pte_t *pte;
    // new mmap_region 的产生
    for(int a = begin;a< begin+PAGESIZE*npages;a+=PAGESIZE)
    {
        if(pte=vm_getpte(myproc()->pgtbl,a,false)==0)
            panic("uvm_munmap: vm_getpte failed");
        if((*pte & PTE_V) ==0)
            panic("uvm_munmap: page not present");
        uint64 pa = PTE_TO_PA(*pte);
        pmem_free(pa, false);
        *pte =0;
    }
    // 尝试合并 mmap_region

    // 页表释放

}

// 用户堆空间增加, 返回新的堆顶地址 (注意栈顶最大值限制)
// 在这里无需修正 p->heap_top
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    uint64 new_heap_top = heap_top + len;
    new_heap_top = PG_ROUND_DOWN(new_heap_top);

    
    for(uint64 a = heap_top; a < new_heap_top; a += PAGESIZE)
    {
        uint64 page = (uint64)pmem_alloc(false);
        if(!page)
        {
            // 分配失败，回滚已分配的页面
            for(uint64 b = heap_top; b < a; b += PAGESIZE)
            {
                pte_t* pte = vm_getpte(pgtbl, b, false);
                if(pte && ((*pte) & PTE_V))
                {
                    uint64 pa = PTE_TO_PA(*pte);
                    pmem_free(pa, false);
                    *pte = 0;
                    sfence_vma(b);
                }
            }
            return heap_top; // 返回旧堆顶
        }
        vm_mappages(pgtbl, a, page, PAGESIZE, PTE_R | PTE_W | PTE_U);
    }

    return new_heap_top;
}

// 用户堆空间减少, 返回新的堆顶地址
// 在这里无需修正 p->heap_top
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    uint64 new_heap_top = heap_top - len;
    new_heap_top = PG_ROUND_UP(new_heap_top);
    
    for(uint64 a = new_heap_top; a < heap_top; a += PAGESIZE)
    {
        pte_t* pte = vm_getpte(pgtbl, a, false);
        if(pte && ((*pte) & PTE_V))
        {
            uint64 pa = PTE_TO_PA(*pte);
            pmem_free(pa, false);
            *pte = 0;
            sfence_vma(a);
        }
    }
    

    return new_heap_top;
}

// 用户态地址空间[src, src+len) 拷贝至 内核态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
void uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{

}

// 内核态地址空间[src, src+len） 拷贝至 用户态地址空间[dst, dst+len)
void uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{

}

// 用户态字符串拷贝到内核态
// 最多拷贝maxlen字节, 中途遇到'\0'则终止
// 注意: src dst 不一定是 page-aligned
int uvm_copyin_str(pgtbl_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;
  uint64 total = 0;

  while (got_null == 0 && max > 0) {
    va0 = PG_ROUND_DOWN(srcva);
    pte_t* pte = vm_getpte(pagetable, va0, false);
    pa0 = PTE2PA(*pte);
    if (pa0 == 0) {
      
      return -1;
    }

    n = PGSIZE - (srcva - va0);
    if (n > max) n = max;

    char *p = (char *)(pa0 + (srcva - va0)); // 注意：若需要 pa2kva，这里自己替换

   

    while (n > 0) {
      unsigned char c = *(unsigned char *)p;
      if (c == '\0') {
        *dst = '\0';
        got_null = 1;
      
        break;
      } else {
        *dst = (char)c;
        total++;
      }
      --n; --max; p++; dst++;
    }

    srcva = va0 + PGSIZE; // 下一页
  }

  if (got_null) {
    return 0;
  } else {
    return -1;
  }
}

int uvmcopy(pgtbl_t old, pgtbl_t new, uint64 heap_top,uint32 ustack_pages)
{
   
    pte_t *pte;
    uint64 pa, i;
    uint64 flags;
    char *mem;
    
    for( i =PAGESIZE;i<heap_top;i+=PAGESIZE)
    {
        if((pte = vm_getpte(old, i, false))==0)
        {
            panic("uvmcopy: vm_getpte failed");
        }
        if((*pte & PTE_V) ==0)
        {
            panic("uvmcopy: page not present");
        }
        pa = PTE_TO_PA(*pte);
        flags = PTE_FLAGS(*pte);
        if((mem= (char*)pmem_alloc(false))==0)
        {
            goto err;
        }
        memmove(mem, (char*)pa, PAGESIZE);
        vm_mappages(new, i, (uint64)mem, PAGESIZE, flags);
    }
    //用户栈复制
    uint64 stack_top = TRAPFRAME;
    uint64 stack_base = stack_top - ustack_pages * PGSIZE;
    for( i = stack_base;i<stack_top;i+=PAGESIZE)
    {
        if((pte = vm_getpte(old, i, false))==0)
        {
            panic("uvmcopy: vm_getpte failed for ustack");
        }
        if((*pte & PTE_V) ==0)
        {
            panic("uvmcopy: page not present for ustack");
        }
        pa = PTE_TO_PA(*pte);
        flags = PTE_FLAGS(*pte);
        if((mem= (char*)pmem_alloc(false))==0)
        {
            goto err;
        }
        memmove(mem, (char*)pa, PAGESIZE);
        vm_mappages(new, i, (uint64)mem, PAGESIZE, flags);
    }
    return 0;

err:
    uvm_munmap(0, i / PAGESIZE);
    return -1;
}