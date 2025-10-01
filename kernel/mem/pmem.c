#include "mem/pmem.h"
#include "lib/lock.h"
#include "lib/print.h"


#define PGROUNDUP(x)  (((x) + PAGESIZE - 1) & ~(PAGESIZE - 1))
#define PGROUNDDOWN(x) ((x) & ~(PAGESIZE - 1))

// 物理页节点
typedef struct page_node { 
struct page_node* next;
} page_node_t; 


// 许多物理页构成一个可分配的区域
typedef struct alloc_region { 
    uint64 begin; // 起始物理地址
    uint64 end; // 终止物理地址
    spinlock_t lk; // 自旋锁(保护下面两个变量)
    uint32 allocable; // 可分配页面数
    page_node_t list_head; // 可分配链的链头节点
} alloc_region_t;


// 内核和用户可分配的物理页分开
static alloc_region_t kernel_region, user_region;



//辅助函数，负责初始化内存
static void region_build_free_list(alloc_region_t* r, uint64 lo, uint64 hi)
{
    uint64 p = PGROUNDUP(lo);
    uint64 e = PGROUNDDOWN(hi);

    r->allocable = 0;
    r->list_head.next = NULL;

    for(; p + PAGESIZE <= e; p += PAGESIZE)
    {
       page_node_t* node = (page_node_t*)(uintptr_t)p; // 直接映射假设???有待商议
        //头插法
        node->next = r->list_head.next;
        r->list_head.next = node;
        // 可分配页面数加一
        r->allocable++;
    }
}

static  bool page_in_region(const alloc_region_t* r, uint64 pa) {
    if (pa % PAGESIZE) return false;//物理地址必须是页对齐的
    return (pa >= r->begin) && (pa + PAGESIZE <= r->end);
}



// 物理内存初始化
void  pmem_init()
{
    // 由链接脚本给出的三个点，推导真正的“可分配范围”
    uint64 kdata = (uint64)(uintptr_t)KERNEL_DATA;
    uint64 a_lo  = (uint64)(uintptr_t)ALLOC_BEGIN;
    uint64 a_hi  = (uint64)(uintptr_t)ALLOC_END;

    uint64 free_lo = PGROUNDUP(kdata > a_lo ? kdata : a_lo);
    uint64 free_hi = PGROUNDDOWN(a_hi);

    if (free_lo >= free_hi) {
        // 没有可分配物理页，清零后返回
        memset(&kernel_region, 0, sizeof(kernel_region));
        memset(&user_region, 0, sizeof(user_region));
        return;
    }

    // 策略：1：2
    uint64 mid = free_lo + (free_hi - free_lo) / 3;

    kernel_region.begin = free_lo;
    kernel_region.end   = mid;
    spinlock_init(&kernel_region.lk, "kern_pmem");

    user_region.begin   = mid;
    user_region.end     = free_hi;
    spinlock_init(&user_region.lk, "user_pmem");

    region_build_free_list(&kernel_region, kernel_region.begin, kernel_region.end);
    region_build_free_list(&user_region,   user_region.begin,   user_region.end);
}

void* pmem_alloc(bool in_kernel)
{
    alloc_region_t* r = in_kernel ? &kernel_region : &user_region;//先判断在哪个区域
    spinlock_acquire(&r->lk);
    //从链表头部摘下一个节点
    page_node_t* node = r->list_head.next;
    if (node) {
        r->list_head.next = node->next;
        r->allocable--;
    }
    spinlock_release(&r->lk);

    if (!node) return NULL;

    return (void*)node;                      // 直接映射：VA==PA
}

void  pmem_free(uint64 page, bool in_kernel)
{
     alloc_region_t* r = in_kernel ? &kernel_region : &user_region;

    //确保归还到正确的池，且页对齐/在区间内
    if (!page_in_region(r, page)) {
        // 也可 panic("pmem_free: bad page/region");
        printf("Warning: pmem_free: bad page/region %p\n", (void*)page);
         printf("2\n");
        return;
    }

    page_node_t* node = (page_node_t*)(uintptr_t)page;

    spinlock_acquire(&r->lk);
    // 头插法归还
    node->next = r->list_head.next;
    r->list_head.next = node;
    r->allocable++;
    spinlock_release(&r->lk);
}