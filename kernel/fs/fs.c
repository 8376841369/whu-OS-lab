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
    uint32 ret = 0;

    for (int i = 0; i < BLOCK_SIZE * 2; i++) {
        str[i] = i;  // 或者 str[i] = i; 也行
    }

    // 创建新的inode
    inode_t *nip = inode_create(FT_FILE, 0, 0);
    inode_lock(nip);

    // 第一次查看
    inode_print(nip);

    // 第一次写入
    ret = inode_write_data(nip, 0, BLOCK_SIZE / 2, str, false);
    assert(ret == BLOCK_SIZE / 2, "inode_write_data: fail");

    // 第二次写入
    ret = inode_write_data(
        nip,
        BLOCK_SIZE / 2,
        BLOCK_SIZE + BLOCK_SIZE / 2,
        str + BLOCK_SIZE / 2,
        false
    );
    assert(ret == BLOCK_SIZE + BLOCK_SIZE / 2, "inode_write_data: fail");

    // 一次读取
    ret = inode_read_data(nip, 0, BLOCK_SIZE * 2, tmp, false);
    assert(ret == BLOCK_SIZE * 2, "inode_read_data: fail");

    // 第二次查看
    inode_print(nip);

    inode_unlock_free(nip);

    // 测试
    if (blockcmp(tmp, str) == true)
        printf("success");
    else
        printf("fail");

    while (1) ;
}
