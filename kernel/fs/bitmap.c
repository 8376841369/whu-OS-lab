#include "fs/buf.h"
#include "fs/fs.h"
#include "fs/bitmap.h"
#include "lib/print.h"
#include "fs/inode.h"
#include "common.h"
extern super_block_t sb;

// search and set bit
static uint32 bitmap_search_and_set(uint32 bitmap_block)
{
    buf_t * buf = buf_read(bitmap_block);
    uint8 *bits = buf->data;
    for (uint32 i = 0; i < BLOCK_SIZE * 8; i++) {
        uint32 byte_idx = i >> 3;        // 等价于 i / 8
        uint8  bit_mask = 1u << (i & 7); // 等价于 1 << (i % 8)

        // 如果这一位当前是 0，说明这个资源还空着
        if ((bits[byte_idx] & bit_mask) == 0) {
            // 3. 把这一位设置为 1（占用）
            bits[byte_idx] |= bit_mask;

            // 4. 把修改写回磁盘，保证位图和磁盘一致
            buf_write(buf);

            // 5. 释放 buf
            buf_release(buf);

            // 6. 返回在这个位图块里的 bit 下标
            return i;
        }
    }
    // 7. 如果没有找到，释放 buf 并返回错误码
    buf_release(buf);
    return BLOCK_NUM_UNUSED;
}

// unset bit
static void bitmap_unset(uint32 bitmap_block, uint32 num)
{
    if(num>= BLOCK_SIZE * 8)
        panic("bitmap_unset: num out of range");
    buf_t * buf = buf_read(bitmap_block);
    uint8 *bits = buf->data;
    uint32 byte_idx = num >> 3;        // 等价于 i / 8
    uint8  bit_mask = 1u << (num & 7); // 等价于 1 << (i % 8)
    if((bits[byte_idx] & bit_mask) == 0)
        panic("bitmap_unset: bit is already free");
    
    bits[byte_idx] &= ~bit_mask;
    buf_write(buf);
    buf_release(buf);

}

uint32 bitmap_alloc_block()
{
    uint32 bits_per_block = BLOCK_SIZE * 8;
    uint32 bitmap_blocks = (sb.data_blocks + bits_per_block - 1) / bits_per_block;
    
    for(uint32 bi = 0;bi<bitmap_blocks;bi++)
    {
        uint32 bitmap_blockno = sb.data_bitmap_start + bi;
        // 在该 bitmap 块中寻找一个空闲 bit，并置 1
        uint32 bit = bitmap_search_and_set(bitmap_blockno);
        if(bit == BLOCK_NUM_UNUSED) {
            continue;
        }
        // 换算为“逻辑数据块编号”
        uint32 logical = bi * bits_per_block + bit;

        // 可能踩到最后一个 bitmap 块里的“无效 bit”，要排除
        if (logical >= sb.data_blocks) {
            // 把刚刚误设的 bit 撤销，继续找
            bitmap_unset(bitmap_blockno, bit);
            continue;
        }

        // 最终的物理块号 = 数据区起始 + 逻辑编号
        uint32 blockno = sb.data_start + logical;

        // （可选）如果希望新分配的块内容为 0，可以在这里清零：
        buf_t *b = buf_read(blockno);
        memset(b->data, 0, BLOCK_SIZE);
        buf_write(b);
        buf_release(b);

        return blockno;
    }
    // 没有任何空闲的数据块
    panic("bitmap_alloc_block: no free data block");
    // 或者：return BLOCK_NUM_UNUSED;
}

void bitmap_free_block(uint32 block_num)
{
    // 1. 检查 block_num 是否在数据区范围内
    if (block_num < sb.data_start || 
        block_num >= sb.data_start + sb.data_blocks) {
        panic("bitmap_free_block: invalid block_num");
    }
    // 2. 计算逻辑数据块编号
    uint32 logical = block_num - sb.data_start;
    // 3. 一个 bitmap_block 能表示多少个数据块
    uint32 bits_per_block = BLOCK_SIZE * 8;
    
    // 4. 计算该逻辑块对应的位图块索引 bi 以及在该块中的 bit 下标 bit
    uint32 bi  = logical / bits_per_block;   // 第几个 bitmap block
    uint32 bit = logical % bits_per_block;   // 这个 bitmap block 内第几个 bit
    // 5. 对应的位图块物理块号
    uint32 bitmap_blockno = sb.data_bitmap_start + bi;
    // 6. 在这个位图块中把该 bit 清零（从1→0，释放）
    bitmap_unset(bitmap_blockno, bit);
    // （可选）如果你想防止“空闲块还留着脏数据”，可以顺便把这个数据块清零：
    buf_t *b = buf_read(block_num);
    memset(b->data, 0, BLOCK_SIZE);
    buf_write(b);
    buf_release(b);
}

uint16 bitmap_alloc_inode()
{
    uint32 bits_per_block = BLOCK_SIZE * 8;
    uint32 inode_count = sb.inode_blocks * INODE_PER_BLOCK;
    // 2. inode 位图需要多少块
    uint32 bitmap_blocks = (inode_count + bits_per_block - 1) / bits_per_block;// 向上取整
    // 3. 从第一个 inode bitmap block 开始，依次寻找空闲 inode
    for (uint32 bi = 0; bi < bitmap_blocks; bi++) {
        uint32 bitmap_blockno = sb.inode_bitmap_start + bi;

        // 在这个 bitmap 块中找一个 0 → 1 的 bit
        uint32 bit = bitmap_search_and_set(bitmap_blockno);
        if (bit == BLOCK_NUM_UNUSED) {   // 说明这个 bitmap 块满了
            continue;
        }

        // 4. 得到逻辑 inode 编号
        uint32 ino = bi * bits_per_block + bit;

        // 5. 防止最后一个 bitmap 块多出来的 “无效 bit”
        if (ino >= inode_count) {
            // 撤销刚刚 set 的这一位
            bitmap_unset(bitmap_blockno, bit);
            continue;
        }

        // 6. 根据你的设计，可以直接用 0-based 的 ino，
        //    也可以统一返回从 1 开始的 inode 号（比如保留0号不用）
        return (uint16)ino;  // 或者 (uint16)(ino + 1) 看你整体设计
    }

    // 7. 走到这一步，说明所有 inode 都被分配完了
    panic("bitmap_alloc_inode: no free inode");
    // 或者 return INODE_NUM_UNUSED;
}

void bitmap_free_inode(uint16 inode_num)
{

    uint32 ino = inode_num;   

    uint32 bits_per_block = BLOCK_SIZE * 8;

    // inode 总数 = inode 区块数 * 每块可存多少个 inode
    uint32 inode_count = sb.inode_blocks * INODE_PER_BLOCK;

    // 1. 合法性检查：不能释放超出范围的 inode
    if (ino >= inode_count) {
        panic("bitmap_free_inode: invalid inode_num");
    }

    // 2. 计算它在 inode 位图中的位置
    // bi: 第几个 bitmap block
    // bit: 该 bitmap block 内的第几个 bit
    uint32 bi  = ino / bits_per_block;
    uint32 bit = ino % bits_per_block;

    // 3. 对应的 inode bitmap 物理块号
    uint32 bitmap_blockno = sb.inode_bitmap_start + bi;

    // 4. 在该 bitmap 块中把对应 bit 从 1 清成 0（释放这个 inode）
    bitmap_unset(bitmap_blockno, bit);
}

// 打印所有已经分配出去的bit序号(序号从0开始)
// for debug
void bitmap_print(uint32 bitmap_block_num)
{
    uint8 bit_cmp;
    uint32 byte, shift;

    printf("\nbitmap:\n");

    buf_t* buf = buf_read(bitmap_block_num);
    for(byte = 0; byte < BLOCK_SIZE; byte++) {
        bit_cmp = 1;
        for(shift = 0; shift <= 7; shift++) {
            if(bit_cmp & buf->data[byte])
               printf("bit %d is alloced\n", byte * 8 + shift);
            bit_cmp = bit_cmp << 1;
        }
    }
    printf("over\n");
    buf_release(buf);
}