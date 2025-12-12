#include "fs/buf.h"
#include "dev/vio.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "lib/string.h"
#include "common.h"

#define N_BLOCK_BUF 64


// 将buf包装成双向循环链表的node
typedef struct buf_node {
    buf_t buf;
    struct buf_node* next;
    struct buf_node* prev;
} buf_node_t;

// buf cache
static buf_node_t buf_cache[N_BLOCK_BUF];
static buf_node_t head_buf; // ->next 已分配 ->prev 可分配
static spinlock_t lk_buf_cache; // 这个锁负责保护 链式结构 + buf_ref + block_num

// 链表操作
static void insert_head(buf_node_t* buf_node, bool head_next)
{
    // 离开
    if(buf_node->next && buf_node->prev) {
        buf_node->next->prev = buf_node->prev;
        buf_node->prev->next = buf_node->next;
    }

    // 插入
    if(head_next) { // 插入 head->next
        buf_node->prev = &head_buf;
        buf_node->next = head_buf.next;
        head_buf.next->prev = buf_node;
        head_buf.next = buf_node;        
    } else { // 插入 head->prev
        buf_node->next = &head_buf;
        buf_node->prev = head_buf.prev;
        head_buf.prev->next = buf_node;
        head_buf.prev = buf_node;
    }
}

static inline buf_node_t* buf2node(buf_t *b)
{
    return container_of(b, buf_node_t, buf);
}

// 初始化
void buf_init()
{
    spinlock_init(&lk_buf_cache, "buf_cache");
    head_buf.next = &head_buf;
    head_buf.prev = &head_buf;

    for(int i = 0;i<N_BLOCK_BUF;i++)
    {
        buf_node_t *bn = &buf_cache[i];
        bn->next = NULL;
        bn->prev = NULL;

        memset(&bn->buf, 0, sizeof(buf_t));
        bn->buf.block_num = BLOCK_NUM_UNUSED;
        sleeplock_init(&bn->buf.slk, "buf_sleeplock");
        bn->buf.buf_ref = 0;
        bn->buf.disk = false;

        insert_head(bn, false); // 插入可分配链表

    }
}

/*
    首先假设这个block_num对应的block在内存中有备份, 找到它并上锁返回
    如果找不到, 尝试申请一个无人使用的buf, 去磁盘读取对应block并上锁返回
    如果没有空闲buf, panic报错
    (建议合并xv6的bget())
*/
buf_t* buf_read(uint32 block_num)
{
    
    spinlock_acquire(&lk_buf_cache);

    // 1. 在整个链表中查找已有的缓存（cache hit）
    buf_node_t *bn;
    for (bn = head_buf.next; bn != &head_buf; bn = bn->next) {
        buf_t *b = &bn->buf;
        if (b->block_num == block_num) {
            b->buf_ref++;
            spinlock_release(&lk_buf_cache);
            printf("buf_get: cache hit for block %d\n", block_num);
            acquiresleep(&b->slk);
            return b;
        }
    }

    // 2. 没有命中：从尾部开始找一个空闲 buf（ref == 0），用于复用（LRU）
    for (bn = head_buf.prev; bn != &head_buf; bn = bn->prev) {
        buf_t *b = &bn->buf;
        if (b->buf_ref == 0) {
            // 在持锁状态下，先把它“占住”
            b->buf_ref = 1;
            uint32 old_block = b->block_num;
            // 这里不改 block_num，先记住旧值，后面要用它写回
            spinlock_release(&lk_buf_cache);
            // 独占这个 buf 的睡眠锁
             printf("slk.locked=%d block=%d buf=%p\n", b->slk.locked, block_num, b);
            acquiresleep(&b->slk);
            
            // 如果这个 buf 以前装过某个块（不是 UNUSED），先把旧块写回磁盘
            if (old_block != BLOCK_NUM_UNUSED) {
                
                b->block_num = old_block;
                virtio_disk_rw(b, true);   // 写回旧块
            }
           
            // 绑定到新的 block_num，并从磁盘读入新块内容
            b->block_num = block_num;
            virtio_disk_rw(b, false);      // 读入新块
            printf("buf_get: cache miss for block %d (reused block %d)\n", block_num, old_block);
            return b;
        }
    }

    spinlock_release(&lk_buf_cache);
    panic("buf_read: no free buf");
}



// 写函数 (强制磁盘和内存保持一致)
void buf_write(buf_t* buf)
{
    if(!buf)
        panic("buf_write: null buf");
    if(!sleeplock_holding(&buf->slk))
        panic("buf_write: buf is locked");
    virtio_disk_rw(buf, true);
}

// buf 释放
void buf_release(buf_t* buf)
{
   if(!sleeplock_holding(&buf->slk))
         panic("buf_release: buf is not locked");
    
    releasesleep(&buf->slk);
    spinlock_acquire(&lk_buf_cache);
    buf->buf_ref--;
    if(buf->buf_ref < 0)
        panic("buf_release: buf_ref < 0");
    if(buf->buf_ref == 0)
    {
        buf_node_t *node = buf2node(buf);
        insert_head(node, true);
    }
    spinlock_release(&lk_buf_cache);
}

// 输出buf_cache的情况
void buf_print()
{
    printf("\nbuf_cache:\n");
    buf_node_t* buf = head_buf.next;
    spinlock_acquire(&lk_buf_cache);
    while(buf != &head_buf)
    {
        buf_t* b = &buf->buf;
        printf("buf %d: ref = %d, block_num = %d\n", (int)(buf-buf_cache), b->buf_ref, b->block_num);
        for(int i = 0; i < 8; i++)
            printf("%d ",b->data[i]);
        printf("\n");
        buf = buf->next;
    }
    spinlock_release(&lk_buf_cache);
}