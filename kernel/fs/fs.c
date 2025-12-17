#include "fs/fs.h"
#include "fs/buf.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/dir.h"
#include "lib/string.h"
#include "lib/print.h"

// 超级块在内存的副本
super_block_t sb;

#define FS_MAGIC 0x12345678
#define SB_BLOCK_NUM 0

// 输出super_block的信息
static void sb_print()
{
    printf("\nsuper block information:\n");
    printf("magic = %x\n", sb.magic);
    printf("block size = %d\n", sb.block_size);
    printf("inode blocks = %d\n", sb.inode_blocks);
    printf("data blocks = %d\n", sb.data_blocks);
    printf("total blocks = %d\n", sb.total_blocks);
    printf("inode bitmap start = %d\n", sb.inode_bitmap_start);
    printf("inode start = %d\n", sb.inode_start);
    printf("data bitmap start = %d\n", sb.data_bitmap_start);
    printf("data start = %d\n", sb.data_start);
}

static uint8 str[2 * BLOCK_SIZE];
static uint8 tmp[2 * BLOCK_SIZE];

static bool blockcmp(const uint8 *a, const uint8 *b) {
    return memcmp(a, b, 2 * BLOCK_SIZE) == 0;
}

// 文件系统初始化
void fs_init()
{
    buf_init();

    buf_t *buf;
    buf = buf_read(SB_BLOCK_NUM);

    memmove(&sb, buf->data, sizeof(sb));
    assert(sb.magic == FS_MAGIC, "fs_init: magic");
    assert(sb.block_size == BLOCK_SIZE, "fs_init: block size");
    buf_release(buf);
    sb_print();

    inode_init();
    // 获取根目录
inode_t* ip = inode_alloc(INODE_ROOT);
inode_lock(ip);

// 第一次查看
dir_print(ip);

// add entry
dir_add_entry(ip, 1, "a.txt");
dir_add_entry(ip, 2, "b.txt");
dir_add_entry(ip, 3, "c.txt");

// 第二次查看
dir_print(ip);

// 第一次检查
assert(dir_search_entry(ip, "b.txt") == 2, "error-1");

// delete entry
dir_delete_entry(ip, "a.txt");

// 第三次查看
dir_print(ip);

// add entry
dir_add_entry(ip, 1, "d.txt");

// 第四次查看
dir_print(ip);

// 第二次检查：重复名字应失败（返回 BLOCK_SIZE）
assert(dir_add_entry(ip, 4, "d.txt") == BLOCK_SIZE, "error-2");

inode_unlock(ip);

printf("over");
while (1);

}
