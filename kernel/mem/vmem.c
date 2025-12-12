#include "mem/vmem.h"
#include "mem/pmem.h"
#include "lib/print.h"
#include "memlayout.h"
#include "riscv.h"

//目前的问题：如果写在同意PA，释放时会出现UAF
//现在不允许做修改权限的事情，因为不会重置TLB
//默认VA==PA

#define REG_BASE   0x10000000UL               
#define REG_SIZE   0x10000000UL             
#define MEM_BASE   0x80000000UL                
#define MEM_SIZE   (128UL * 1024 * 1024)       // 128MB

static inline void tlb_flush_va(uint64 va){ sfence_vma(va); }
static inline void tlb_flush_all(void){ sfence_vma(); }


pgtbl_t kernel_pgtbl = 0;

// in trampoline.S
extern char trampoline[];



// 假设现在 VA==PA，因此这两个转换是 no-op；未来可改成带偏移的实现



void   vm_print(pgtbl_t pgtbl)
{
    uint64 scan_limit = 1ul << 38;
    for (uint64 va = 0; va < scan_limit; va += PAGESIZE) {
        pte_t* pte = vm_getpte(pgtbl, va, false);
        if (pte && (*pte & PTE_V)) {
            uint64 pa = PTE_TO_PA(*pte);
            int flags = PTE_FLAGS(*pte);
            // printf
            printf("VA %p -> PA %p flags 0x%x\n", va, pa, flags);
        }
    }
}


// 返回va对应的pte指针, 如果alloc为true, 则在需要时分配页表
pte_t* vm_getpte(pgtbl_t root, uint64 va, bool alloc)
{
    if(root == NULL)
    {
        root = kernel_pgtbl;//如果传入的根页表为空，则使用内核页表
    }
    if(va>=VA_MAX)//如果va超过了虚拟地址范围
    {
        panic("vitual address out of range");
        return NULL;
    }

    pgtbl_t pgtbl = root;//保存根页表地址

    for(int level=2;level>0;level--)//三级页表的前两级
    {
        uint64 vpn = VA_TO_VPN(va,level);
        pte_t* pte = &pgtbl[vpn];

        if(*pte & PTE_V)//页表有效
        {
            if(*pte&(PTE_R|PTE_W|PTE_X))
            {
                // 命中了上层叶子（superpage），直接返回
                return pte;
            }
            uint64 child_pa = PTE_TO_PA(*pte);//取出子页表的物理地址
            pgtbl = (pgtbl_t)pa2kva(child_pa);//转换成内核虚拟地址，默认VA==PA
        }
        else
        {
            if(!alloc) return NULL;//不分配，直接返回NULL

            void *new_page = pmem_alloc(true);//分配一个物理页
            if(!new_page)
            {
                printf("vm_getpte: pmem_alloc failed for virtual memory=0x%lx\n", va);
                return NULL;
            }
            memset(new_page,0,PAGESIZE);
            *pte = PA_TO_PTE(kva2pa(new_page)) | PTE_V; //修改这个表项，设置新分配的页的有效位
            pgtbl = (pgtbl_t)new_page;//去新的页表继续
        }
    }
    return &pgtbl[VA_TO_VPN(va,0)];//返回最后一级页表项的地址

}

void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm)//映射len长度的虚拟地址到物理地址，权限为perm
{
    if(len==0) return;//长度为0，直接返回

    uint64 start = va;
    uint64 end = va + len;

    while(start < end)
    {
        uint64 a = PG_ROUND_DOWN(start);//对齐到页边界
        pte_t* pte = vm_getpte(pgtbl,a,true);//取出对应的页表项
        if(!pte)
        {
            panic("vm_mappages: vm_getpte failed");
            return;
        }
        // if((*pte & PTE_V) != 0)//已经被映射了
        // {
        //     panic("vm_mappages: remap");
        //     return;
        // }
        // 如果有写权限，则必须有读权限
        if (perm & PTE_W) perm |= PTE_R;
        
        *pte = PA_TO_PTE(pa) | (perm&0x3ff) | PTE_V;//设置页表项

        start = a + PAGESIZE;
        pa += PAGESIZE;
    }
}

void   vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit)
{
    uint64 a = PG_ROUND_DOWN(va);
    uint64 last = PG_ROUND_DOWN(va + len - 1);

    for (;;) {
        pte_t* pte = vm_getpte(pgtbl, a, false); 
        if (pte == NULL || (*pte & PTE_V) == 0) {
            panic("vm_unmappages: not mapped");
        }
        if (PTE_CHECK(*pte)) {
            panic("vm_unmappages: not a leaf pte");
        }
        if (freeit) {
            uint64 pa = PTE_TO_PA(*pte);
            bool in_kernel = page_in_region(&kernel_region, pa);
            pmem_free(pa, in_kernel);
        }

        *pte = 0;
        sfence_vma(a);//刷新对应的TLB项

        if (a == last)
            break;

        a += PAGESIZE;
    }
}



void kvm_init()
{
    kernel_pgtbl = (pgtbl_t)pmem_alloc(true);
    if (!kernel_pgtbl) {
        panic("kvm_init: pmem_alloc failed");
    }
   
    memset(kernel_pgtbl, 0, PAGESIZE);
    vm_mappages(kernel_pgtbl, REG_BASE, REG_BASE, REG_SIZE, PTE_R | PTE_W);
    vm_mappages(kernel_pgtbl, MEM_BASE, MEM_BASE, MEM_SIZE, PTE_R | PTE_W | PTE_X);
    // trampoline 映射
    uint64 trampoline_pa = kva2pa((void*)trampoline);
    
    vm_mappages(kernel_pgtbl, (uint64)TRAMPOLINE, trampoline_pa, PAGESIZE, PTE_A|PTE_V|PTE_R | PTE_X);
    //UART映射
    vm_mappages(kernel_pgtbl,UART_BASE,UART_BASE,PAGESIZE,PTE_R | PTE_W);
    // PLIC映射
    vm_mappages(kernel_pgtbl, PLIC_BASE, PLIC_BASE, 0x400000, PTE_R | PTE_W);
    
  
}

void kvm_inithart()
{
    w_satp(MAKE_SATP(kernel_pgtbl));
    //flush TLB
    sfence_vma();
}