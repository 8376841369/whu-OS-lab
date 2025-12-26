#include "fs/buf.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/fs.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "lib/print.h"
#include "lib/string.h"

extern super_block_t sb;

// 内存中的inode资源 + 保护它的锁
#define N_INODE 32
static inode_t icache[N_INODE];
static spinlock_t lk_icache;


// inode.c 顶部内部定义一个“磁盘版 inode”结构
typedef struct disk_inode {
    uint16 type;
    uint16 major;
    uint16 minor;
    uint16 nlink;
    uint32 size;
    uint32 addrs[N_ADDRS];
} disk_inode_t;

// icache初始化
void inode_init()
{
    spinlock_init(&lk_icache, "icache");
    for(int i = 0;i<N_INODE;i++)
    {
        inode_t *ip = &icache[i];
        // 磁盘 inode 信息部分初始化为“未使用”
        ip->type  = FT_UNUSED;
        ip->major = 0;
        ip->minor = 0;
        ip->nlink = 0;
        ip->size  = 0;
        for (int j = 0; j < N_ADDRS; ++j) {
            ip->addrs[j] = 0;
        }

        // 内存 inode 信息部分
        ip->inode_num = INODE_NUM_UNUSED;  // 表示还没绑定具体 inode 号
        ip->ref       = 0;                 // 当前没人使用
        ip->valid     = false;             // 磁盘数据尚未加载

        // 初始化这个 inode 自己的睡眠锁
        sleeplock_init(&ip->slk, "inode");
    }
}

/*---------------------- 与inode本身相关 -------------------*/

// 使用磁盘里的inode更新内存里的inode (write = false)
// 或 使用内存里的inode更新磁盘里的inode (write = true)
// 调用者需要设置inode_num并持有睡眠锁
void inode_rw(inode_t* ip, bool write)
{
    if (ip == NULL)
        return;
    if (ip->inode_num == INODE_NUM_UNUSED) {
        panic("inode_rw: bad inode_num");
    }

    uint32 inum  = ip->inode_num;
    uint32 bno   = sb.inode_start + inum / INODE_PER_BLOCK;
    uint32 index = inum % INODE_PER_BLOCK;

    buf_t* bp = buf_read(bno);
    if (bp == NULL) {
        return;
    }
    disk_inode_t *dip = (disk_inode_t *)(bp->data + index * INODE_DISK_SIZE);

    if (!write) {
        // ===== 磁盘 → 内存 =====
        ip->type  = dip->type;
        ip->major = dip->major;
        ip->minor = dip->minor;
        ip->nlink = dip->nlink;
        ip->size  = dip->size;
        for (int i = 0; i < N_ADDRS; ++i) {
            ip->addrs[i] = dip->addrs[i];
        }
        ip->valid = true;   // 已经和磁盘同步
        // 不需要 buf_write
    } else {
        // ===== 内存 → 磁盘 =====
        dip->type  = ip->type;
        dip->major = ip->major;
        dip->minor = ip->minor;
        dip->nlink = ip->nlink;
        dip->size  = ip->size;
        for (int i = 0; i < N_ADDRS; ++i) {
            dip->addrs[i] = ip->addrs[i];
        }

        buf_write(bp);      // 把包含这个 inode 的 block 写回磁盘
    }

    buf_release(bp);
}

// 在icache里查询inode
// 如果没有查询到则申请一个空闲inode
// 如果icache没有空闲inode则报错
// 注意: 获得的inode没有上锁
inode_t* inode_alloc(uint16 inode_num)
{    
    if (inode_num == INODE_NUM_UNUSED) {
        return NULL;
    }

    spinlock_acquire(&lk_icache);

    inode_t *ip = NULL;

    // 1. 先查找是否已经在 icache 中（cache hit）
    for (int i = 0; i < N_INODE; ++i) {
        inode_t *cur = &icache[i];
        if (cur->ref > 0 && cur->inode_num == inode_num) {
            // 找到已有 inode，增加引用计数
            cur->ref++;
            ip = cur;
            spinlock_release(&lk_icache);
            return ip;
        }
    }

    // 1. 先查找是否已经在 icache 中（cache hit）
    for (int i = 0; i < N_INODE; ++i) {
        inode_t *cur = &icache[i];
        if (cur->ref > 0 && cur->inode_num == inode_num) {
            // 找到已有 inode，增加引用计数
            cur->ref++;
            ip = cur;
            spinlock_release(&lk_icache);
            return ip;
        }
    }

    // 2. 没找到，则寻找一个空闲槽位（ref == 0）
    for (int i = 0; i < N_INODE; ++i) {
        inode_t *cur = &icache[i];
        if (cur->ref == 0) {
            // 绑定新的 inode_num
            cur->inode_num = inode_num;
            cur->ref       = 1;
            cur->valid     = false;   // 磁盘元数据还未加载

            // 磁盘字段 type/size/addrs[] 会在 inode_lock 时通过 inode_rw 读入
            ip = cur;
            spinlock_release(&lk_icache);
            return ip;
        }
    }

    // 3. 没有空闲inode，直接panic
    spinlock_release(&lk_icache);
    panic("inode_alloc: no free inode in icache");
    return NULL; // 不会执行到这里，只是为了消除编译器警告
}

// 在磁盘里申请一个inode (操作bitmap, 返回inode_num)
// 向icache申请一个inode数据结构
// 填写内存里的inode并以此更新磁盘里的inode
// 注意: 获得的inode没有上锁
inode_t* inode_create(uint16 type, uint16 major, uint16 minor)
{
        // 1. 在磁盘 inode 位图中分配一个新的 inode 号
    uint16 inum = bitmap_alloc_inode();
    if (inum == INODE_NUM_UNUSED) {
        // 磁盘里没有空闲 inode 了
        // 你可以选择 panic 或返回 NULL，看整体风格
        printf("inode_create: no free inode in bitmap\n");
        return NULL;
    }
    // 2. 在内存 icache 中为这个 inode 号申请一个 inode_t 结构
    inode_t *ip = inode_alloc(inum);
    if (ip == NULL) {
        // icache 里竟然分不到 slot，需要回滚位图的分配
        bitmap_free_inode(inum);
        printf("inode_create: no free inode in icache\n");
        return NULL;
    }

    // 3. 初始化内存 inode，并写回磁盘
    //    注意: 这里需要持有 ip->slk，因为要修改 type/size/addrs 等元数据
    inode_lock(ip);

    ip->type  = type;
    ip->major = major;
    ip->minor = minor;

    // 对于新建的一般文件/目录，nlink 通常先设为 1
    // 后续若创建更多硬链接，调用者会再增加 nlink 并写回
    ip->nlink = 1;

    ip->size  = 0;

    // 数据块地址全部清零
    for (int i = 0; i < N_ADDRS; ++i) {
        ip->addrs[i] = 0;
    }

    // 这个时刻起，内存中的 inode 元数据已经准备好
    ip->valid = true;

    // 用内存中的 ip 内容更新磁盘 inode 表
    inode_rw(ip, true);

    // 4. 按注释要求：返回的 inode 不加锁
    inode_unlock(ip);

    return ip;
}

// 供inode_free调用
// 在磁盘上删除一个inode及其管理的文件 (修改inode bitmap + block bitmap)
// 调用者需要持有lk_icache, 但不应该持有slk
static void inode_destroy(inode_t* ip)
{
    if (ip == NULL)
        return;

    // 一般这里应该只在 ref == 0 且 nlink == 0 时被调用
    // 若你愿意，可以加一些防御性检查：
    // if (ip->ref != 0 || ip->nlink != 0) panic("inode_destroy: invalid state");

    // 1. 上 inode 自己的睡眠锁，准备修改磁盘元数据
    inode_lock(ip);   // 内部会在 !valid 时自动 inode_rw(ip, false)

    // 2. 释放这个 inode 管理的所有数据块（更新数据块位图 + 清空 addrs/size）
    //    这个函数应该负责：
    //    - 遍历 ip->addrs[]，对每个非 0 数据块调用 bitmap_free_block()
    //    - 把 ip->addrs[] 全部置 0，ip->size = 0
    //    - 适当调用 inode_rw(ip, true) 把更新写回磁盘
    inode_free_data(ip);

    // 3. 清空 inode 自身的磁盘元数据，标记为未使用
    ip->type  = FT_UNUSED;
    ip->major = 0;
    ip->minor = 0;
    ip->nlink = 0;
    ip->size  = 0;
    for (int i = 0; i < N_ADDRS; ++i) {
        ip->addrs[i] = 0;
    }

    // 把“这个 inode 已经变成 UNUSED”的事实写回到磁盘 inode 表
    inode_rw(ip, true);

    // 4. 在 inode 位图中释放这个 inode 号
    //    之后这个 inum 可以被 inode_create / bitmap_alloc_inode 重新分配
    bitmap_free_inode(ip->inode_num);

    // 5. 更新内存里的状态，让这个 icache 槽位将来可以被复用
    ip->valid     = false;             // 表示不再有有效的磁盘元数据
    ip->inode_num = INODE_NUM_UNUSED;  // 这行可选，看你后续 icache 逻辑

    inode_unlock(ip);
}

// 向icache里归还inode
// inode->ref--
// 调用者不应该持有slk
void inode_free(inode_t* ip)
{
    if (ip == NULL)
        return;

    // 调用者不应该持有睡眠锁，防止锁顺序错误
    if (sleeplock_holding(&ip->slk)) {
        panic("inode_free: holding slk");
    }

    spinlock_acquire(&lk_icache);

    if (ip->ref <= 0) {
        spinlock_release(&lk_icache);
        panic("inode_free: ref <= 0");
    }

    ip->ref--;

    if (ip->ref > 0) {
        // 还有别的引用者，什么都不用做
        spinlock_release(&lk_icache);
        return;
    }

    // 走到这里说明 ref 刚好减到 0，
    // 如果没有任何目录项再指向这个 inode，就可以真正销毁它
    if (ip->nlink == 0) {
        // inode_destroy 要求调用者持有 lk_icache，但不能持有 slk
        inode_destroy(ip);
        // inode_destroy 内部会 inode_lock/ip->数据块释放/inode bitmap 释放/inode_rw 等
        // 以及 inode_unlock
    }

    // 无论是否 destroy，这里都释放 icache 锁
    spinlock_release(&lk_icache);
}

inode_t* inode_dup(inode_t* ip)
{
    if (ip == NULL)
        return NULL;

    // 引用计数由 lk_icache 保护
    spinlock_acquire(&lk_icache);

    if (ip->ref < 1) {
        spinlock_release(&lk_icache);
        panic("inode_dup: ref < 1");
    }

    ip->ref++;

    spinlock_release(&lk_icache);
    return ip;
}

// // 给inode上锁
// // 如果valid失效则从磁盘中读入
// void inode_lock(inode_t* ip)
// {
//     if (ip == NULL)
//         panic("inode_lock: null ip");

//     // 加这个 inode 自己的睡眠锁，保护 type/size/addrs/valid 等字段
//     acquiresleep(&ip->slk);

//     // 第一次使用或被 invalidate 后，需要从磁盘把元数据读入
//     if (!ip->valid) {
//         // 从磁盘 inode 表读出这个 inode 的元数据，写入 ip->type/size/addrs…
//         inode_rw(ip, false);

//         // 这里可以加一个防御性检查：如果磁盘上是未使用 inode，就炸掉
//         // if (ip->type == FT_UNUSED) {
//         //     releasesleep(&ip->slk);
//         //     panic("inode_lock: no such inode on disk");
//         // }
//         // inode_rw 里已经会把 ip->valid 置为 true
//     }
// }

void inode_lock(inode_t* ip)
{
    acquiresleep(&ip->slk);
    if(!ip->valid){
        inode_rw(ip, false);
    }
}
// 给inode解锁
void inode_unlock(inode_t* ip)
{
    if (ip == NULL)
        panic("inode_unlock: null ip");
    if (!sleeplock_holding(&ip->slk))
        panic("inode_unlock: inode not locked");
    releasesleep(&ip->slk);
}

// 连招: 解锁 + 释放
void inode_unlock_free(inode_t* ip)
{
    if (ip == NULL)
        return;

    // 按约定：调用 inode_unlock_free 之前必须已经持有 slk
    if (!sleeplock_holding(&ip->slk)) {
        panic("inode_unlock_free: inode not locked");
    }
    // 1. 先释放这个 inode 的睡眠锁
    releasesleep(&ip->slk);

    // 2. 再把引用计数 -1，如有必要触发 inode_destroy
    inode_free(ip);
}

/*---------------------------- 与inode管理的data相关 --------------------------*/

// 辅助 inode_locate_block
// 递归查询或创建block
static uint32 locate_block(uint32* entry, uint32 bn, uint32 size)
{
    if(*entry == 0)
        *entry = bitmap_alloc_block();

    if(size == 1)
        return *entry;    

    uint32* next_entry;
    uint32 next_size = size / ENTRY_PER_BLOCK;
    uint32 next_bn = bn % next_size;
    uint32 ret = 0;

    buf_t* buf = buf_read(*entry);
    next_entry = (uint32*)(buf->data) + bn / next_size;
    ret = locate_block(next_entry, next_bn, next_size);
    buf_release(buf);

    return ret;
}

// 确定inode里第bn块data block的block_num
// 如果不存在第bn块data block则申请一个并返回它的block_num
// 由于inode->addrs的结构, 这个过程比较复杂, 需要单独处理
static uint32 inode_locate_block(inode_t* ip, uint32 bn)
{
    // 1. 直接块区域 [0, N_ADDRS_1)
    if (bn < N_ADDRS_1) {
        if (ip->addrs[bn] == 0) {
            ip->addrs[bn] = bitmap_alloc_block();
        }
        return ip->addrs[bn];
    }

    // 剔除 direct 部分
    bn -= N_ADDRS_1;

    // 2. 一级间接块区域：N_ADDRS_2 个根 entry，每个管理 ENTRY_PER_BLOCK 个数据块
    uint32 blocks1 = N_ADDRS_2 * ENTRY_PER_BLOCK;
    if (bn < blocks1) {
        // 属于第几个一级间接根？
        uint32 idx      = N_ADDRS_1 + bn / ENTRY_PER_BLOCK;  // 在 ip->addrs 里的下标
        uint32 inner_bn = bn % ENTRY_PER_BLOCK;              // 在该根下的第几个数据块

        return locate_block(&ip->addrs[idx], inner_bn, ENTRY_PER_BLOCK);
    }

    // 剔除一级间接部分
    bn -= blocks1;

    // 3. 二级间接块区域：N_ADDRS_3 个根 entry，每个管理 ENTRY_PER_BLOCK^2 个数据块
    uint32 per_root = ENTRY_PER_BLOCK * ENTRY_PER_BLOCK;
    uint32 blocks2  = N_ADDRS_3 * per_root;
    if (bn < blocks2) {
        uint32 idx      = N_ADDRS_1 + N_ADDRS_2 + bn / per_root;
        uint32 inner_bn = bn % per_root;

        return locate_block(&ip->addrs[idx], inner_bn, per_root);
    }

    // 4. 超出 inode 能管理的范围
    panic("inode_locate_block: bn too large");
    return 0;  // 为了消
}


// 只查询，不分配：如果中间某一级 entry 为 0，就返回 0
static uint32 locate_block_ro(uint32* entry, uint32 bn, uint32 size)
{
    if (*entry == 0)
        return 0;          // 这一整棵子树都是“洞”

    if (size == 1)
        return *entry;     // entry 本身就是最终数据块号

    uint32 next_size = size / ENTRY_PER_BLOCK;
    uint32 next_bn   = bn % next_size;
    uint32 index     = bn / next_size;

    buf_t* buf = buf_read(*entry);
    uint32* next_entry = (uint32*)(buf->data) + index;
    uint32 ret = locate_block_ro(next_entry, next_bn, next_size);
    buf_release(buf);

    return ret;            // 可能是 0（洞），也可能是实际 block_num
}


// 仅查询inode里第bn块（逻辑块号）data block的block_num
static uint32 inode_get_blocknum(inode_t* ip, uint32 bn)
{
    if(bn < N_ADDRS_1) {
        return ip->addrs[bn];
    }
    
    bn -= N_ADDRS_1;
    uint32 blocks1 = N_ADDRS_2 * ENTRY_PER_BLOCK;
    if(bn < blocks1)
    {
        uint32 idx      = N_ADDRS_1 + bn / ENTRY_PER_BLOCK;
        uint32 inner_bn = bn % ENTRY_PER_BLOCK;
        return locate_block_ro(&ip->addrs[idx], inner_bn, ENTRY_PER_BLOCK);
    }

    bn -= blocks1;
    // 3) double indirect
    uint32 per_root = ENTRY_PER_BLOCK * ENTRY_PER_BLOCK;
    uint32 blocks2  = N_ADDRS_3 * per_root;
    if (bn < blocks2) {
        uint32 idx      = N_ADDRS_1 + N_ADDRS_2 + bn / per_root;
        uint32 inner_bn = bn % per_root;
        return locate_block_ro(&ip->addrs[idx], inner_bn, per_root);
    }

    // 超出 inode 能管理的范围
    return 0;

}

// 读取 inode 管理的 data block
// 调用者需要持有 inode 锁
// 成功返回读出的字节数, 失败返回0
uint32 inode_read_data(inode_t* ip, uint32 offset, uint32 len, void* dst, bool user)
{
    if (ip == NULL)
        return 0;
    // 1. 不允许从文件尾后面读
    if (offset >= ip->size) {
        return 0;
    }
    // 2. 如果请求长度超出文件大小，做截断
    if (offset + len > ip->size) {
        len = ip->size - offset;
    }
    uint32 tot = 0;   // 已经读了多少字节

    while(tot<len)
    {
        uint32 off_in_file = offset + tot;
        uint32 bn   = off_in_file / BLOCK_SIZE;    // 第几个逻辑块
        uint32 boff = off_in_file % BLOCK_SIZE;    // 在该块内的偏移

        uint32 blockno = inode_get_blocknum(ip, bn);
        if (blockno == 0) {
            // 读到了“洞”，直接返回已读字节数
            break;
        }
        buf_t* bp = buf_read(blockno);
        if (!bp) {
            break;  // 理论上不会发生，发生了就提前结束
        }

        // 4. 计算这次最多能读多少字节
        uint32 n1 = BLOCK_SIZE - boff;   // 当前块剩余空间
        uint32 n2 = len - tot;           // 还需要多少
        uint32 n  = (n1 < n2) ? n1 : n2;

        if(!user)
        {
            memmove((char*)dst + tot, bp->data + boff, n);
        }
        else
        {
            uvm_copyout(myproc()->pgtbl, (uint64)(dst) + tot, (uint64)(bp->data + boff), n);
        }
        buf_release(bp);
        tot += n;
    }
    return tot;
}


// 写入 inode 管理的 data block (可能导致管理的 block 增加)
// 调用者需要持有 inode 锁
// 成功返回写入的字节数, 失败返回0
uint32 inode_write_data(inode_t* ip, uint32 offset, uint32 len, void* src, bool user)
{
    if (ip == NULL)
        return 0;

    // 设备文件一般不走这里（如果你有设备层读写接口，应在上层分流）
    // 这里只做一个防御性检查：
    if (ip->type == FT_UNUSED)
        return 0;
    
    // 防止越界（inode 最大可管理空间）
    if (offset > INODE_MAXSIZE)
        return 0;
    if (offset + len > INODE_MAXSIZE)
        len = INODE_MAXSIZE - offset;
    
    uint32 tot = 0;   // 已经写了多少字节

    while(tot < len)
    {
        uint32 off_in_file = offset + tot;
        uint32 bn   = off_in_file / BLOCK_SIZE;   // 第几个逻辑块
        uint32 boff = off_in_file % BLOCK_SIZE;   // 块内偏移

        // 1) 查找/必要时分配第 bn 个数据块对应的物理块号
        uint32 blockno = inode_locate_block(ip, bn);
        if (blockno == 0) {
            // bitmap_alloc_block 失败等情况（如果你实现中会返回0）
            break;
        }

        // 2) 读出该块（为了做“部分写”必须先读）
        buf_t* bp = buf_read(blockno);
        if (!bp) break;

        // 3) 算本轮能写多少
        uint32 n1 = BLOCK_SIZE - boff;  // 当前块剩余空间
        uint32 n2 = len - tot;          // 还需要写多少
        uint32 n  = (n1 < n2) ? n1 : n2;

        if(!user)
        {
            memmove(bp->data + boff, (char*)src + tot, n);
        }
        else
        {
            uvm_copyin(myproc()->pgtbl, (uint64)(bp->data + boff), (uint64)(src) + tot, n);
        }
        buf_write(bp);
        buf_release(bp);
        tot += n;
    }
    // 6) 更新文件大小（写到了文件尾之后才需要扩展）
    if (tot > 0 && offset + tot > ip->size) {
        ip->size = offset + tot;

        // inode 元数据也要落盘，否则重启后 size/addrs 会丢
        // （你的 inode_rw 约定：持有 slk，write=true 写回磁盘 inode 表）
        inode_rw(ip, true);
    } else {
        // 如果你希望“即使不扩容也同步元数据”，也可以写回
        // 通常没必要：addrs/size 没变，就不用 inode_rw(true)
    }

    return tot;

}

// 辅助 inode_free_data 做递归释放
static void data_free(uint32 block_num, uint32 level)
{  
    assert(block_num != 0, "data_free: block_num = 0");

    // block_num 是 data block
    if(level == 0) goto ret;

    // block_num 是 metadata block
    buf_t* buf = buf_read(block_num);
    for(uint32* addr = (uint32*)buf->data; addr < (uint32*)(buf->data + BLOCK_SIZE); addr++) 
    {
        if(*addr == 0) break;
        data_free(*addr, level - 1);
    }
    buf_release(buf);

ret:
    bitmap_free_block(block_num);
    return;
}

// 释放inode管理的 data block
// ip->addrs被清空 ip->size置0
// 调用者需要持有slk
void inode_free_data(inode_t* ip)
{
    if (ip == NULL)
        return;

    // 1) direct blocks: addrs[0 .. N_ADDRS_1-1] 是 data block
    for (int i = 0; i < N_ADDRS_1; i++) {
        if (ip->addrs[i] != 0) {
            data_free(ip->addrs[i], 0);   // level=0: 直接释放数据块
            ip->addrs[i] = 0;
        }
    }

    // 2) single indirect roots: addrs[N_ADDRS_1 .. N_ADDRS_1+N_ADDRS_2-1]
    //    每个根指向一个“索引块”（metadata block），里面是 data block 号
    for (int k = 0; k < N_ADDRS_2; k++) {
        int idx = N_ADDRS_1 + k;
        if (ip->addrs[idx] != 0) {
            data_free(ip->addrs[idx], 1); // level=1: 释放索引块 + 其下所有数据块
            ip->addrs[idx] = 0;
        }
    }

    // 3) double indirect roots: addrs[N_ADDRS_1+N_ADDRS_2 .. N_ADDRS-1]
    //    每个根指向一级索引块（metadata），里面是二级索引块号；二级索引块里是数据块号
    for (int k = 0; k < N_ADDRS_3; k++) {
        int idx = N_ADDRS_1 + N_ADDRS_2 + k;
        if (ip->addrs[idx] != 0) {
            data_free(ip->addrs[idx], 2); // level=2: 释放两层索引 + 所有数据块
            ip->addrs[idx] = 0;
        }
    }

    ip->size = 0;
}

static char* inode_types[] = {
    "INODE_UNUSED",
    "INODE_DIR",
    "INODE_FILE",
    "INODE_DEVICE",
};

// 输出inode信息
// for dubug
void inode_print(inode_t* ip)
{
    assert(sleeplock_holding(&ip->slk), "inode_print: lk");

    printf("\ninode information:\n");
    printf("num = %d, ref = %d, valid = %d\n", ip->inode_num, ip->ref, ip->valid);
    printf("type = %s, major = %d, minor = %d, nlink = %d\n", inode_types[ip->type], ip->major, ip->minor, ip->nlink);
    printf("size = %d, addrs =", ip->size);
    for(int i = 0; i < N_ADDRS; i++)
        printf(" %d", ip->addrs[i]);
    printf("\n");
}