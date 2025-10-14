#ifndef __PMEM_H__
#define __PMEM_H__

#include "common.h"
#include "lib/string.h"
#include "lib/lock.h"


// 来自kernel.ld
extern char KERNEL_DATA[];
extern char ALLOC_BEGIN[];
extern char ALLOC_END[];


// 空闲页节点
typedef struct page_node {
    struct page_node* next;
} page_node_t;

// 可分配区域
typedef struct alloc_region {
    uint64 begin;//起始的物理地址
    uint64 end;//结束的物理地址
    spinlock_t lk;//自旋锁
    uint32  allocable;//可以分配的自旋锁页数
    page_node_t list_head;//头节点
} alloc_region_t;

// ★ 只声明（extern），不定义
extern alloc_region_t kernel_region;
extern alloc_region_t user_region;

// ★ 对外可见的函数原型
bool  page_in_region(const alloc_region_t* r, uint64 pa);


void  pmem_init(void);
void* pmem_alloc(bool in_kernel);
void  pmem_free(uint64 page, bool in_kernel);

#endif