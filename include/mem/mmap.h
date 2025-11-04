#ifndef __MMAP_H__
#define __MMAP_H__

#include "common.h"

typedef struct mmap_region {
    uint64 begin;             // 起始地址
    uint32 npages;            // 管理的页面数量
    struct mmap_region* next; // 链表指针
} mmap_region_t;

void           mmap_init();
mmap_region_t* mmap_region_alloc();
void           mmap_region_free(mmap_region_t* mmap);
void           mmap_show_mmaplist();

#endif