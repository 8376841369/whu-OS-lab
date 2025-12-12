#include "fs/fs.h"
#include "fs/buf.h"
#include "fs/inode.h"
#include "fs/dir.h"
#include "fs/bitmap.h"
#include "lib/string.h"
#include "lib/print.h"
#include "proc/cpu.h"

// 对目录文件的简化性假设: 每个目录文件只包括一个block
// 也就是每个目录下最多 BLOCK_SIZE / sizeof(dirent_t) = 32 个目录项

// 查询一个目录项是否在目录里
// 成功返回这个目录项的inode_num
// 失败返回INODE_NUM_UNUSED
// ps: 调用者需持有pip的锁
uint16 dir_search_entry(inode_t *pip, char *name)
{
    if(pip==NULL || name==NULL) {
        return INODE_NUM_UNUSED;
    }

    // 可选：保证这是个目录
    if (pip->type != FT_DIR) {
        return INODE_NUM_UNUSED;
    }

    // 目录只占一个数据块，数据块号在 addrs[0]
    uint32 data_block = pip->addrs[0];
    if (data_block == 0) {
        // 还没分配数据块，说明目录里还没有任何目录项
        return INODE_NUM_UNUSED;
    }

    // 读入目录这一块
    buf_t *bp = buf_read(data_block);
    if (bp == NULL) {
        // 读失败就当没找到
        return INODE_NUM_UNUSED;
    }

    // 目录项数组
    dirent_t *ents = (dirent_t *)bp->data;

    // 根据 inode 的 size，算出实际的目录项数量
    uint32 n = pip->size / sizeof(dirent_t);
    // 理论上不应该超过一个块能容纳的最大数量，加一道保险
    uint32 max_n = BLOCK_SIZE / sizeof(dirent_t);
    if (n > max_n) {
        n = max_n;
    }

        // 逐个扫描目录项
    for (uint32 i = 0; i < n; i++) {
        // 空闲目录项（没用过），跳过
        if (ents[i].inode_num == INODE_NUM_UNUSED) {
            continue;
        }

        // name[] 在 dir_add_entry 时应当写成以 '\0' 结尾的字符串
        // 这里用 strncmp，最多比较 DIR_NAME_LEN 个字节
        if (strncmp(ents[i].name, name, DIR_NAME_LEN) == 0) {
            uint16 inum = ents[i].inode_num;
            buf_release(bp);
            return inum;
        }
    }

     // 没找到
    buf_release(bp);
    return INODE_NUM_UNUSED;


}

// 在pip目录下添加一个目录项
// 成功返回这个目录项的偏移量 (同时更新pip->size)
// 失败返回BLOCK_SIZE (没有空间 或 发生重名)
// ps: 调用者需持有pip的锁
uint32 dir_add_entry(inode_t *pip, uint16 inode_num, char *name)
{
    // 0. 基本检查
    // Debug 断言：必须已经持有 pip 的睡眠锁
    if (!sleeplock_holding(&pip->slk)) {
        panic("dir_add_entry: pip not locked");
    }
    if (pip == NULL || name == NULL) {
        return BLOCK_SIZE;
    }
    if (pip->type != FT_DIR) {
        return BLOCK_SIZE;
    }
    if (inode_num == INODE_NUM_UNUSED) {
        return BLOCK_SIZE;
    }

    // 1. 确保目录有数据块（目录只占一个 block）
    if (pip->addrs[0] == 0) {
        uint32 bno = bitmap_alloc_block();
        if (bno == 0) {      // 分配失败，看你 bitmap_alloc_block 的错误约定
            return BLOCK_SIZE;
        }
        pip->addrs[0] = bno;

        buf_t *bp0 = buf_read(bno);
        if (bp0 == NULL) {
            // 简单处理：回退分配的块
            bitmap_free_block(bno);
            pip->addrs[0] = 0;
            return BLOCK_SIZE;
        }
        memset(bp0->data, 0, BLOCK_SIZE);
        buf_write(bp0);
        buf_release(bp0);

        pip->size = 0;
        inode_rw(pip, true);   // 把更新后的 inode 写回磁盘
    }
    // 2. 读取目录块
    buf_t *bp = buf_read(pip->addrs[0]);
    if (bp == NULL) {
        return BLOCK_SIZE;
    }

    dirent_t *ents = (dirent_t *)bp->data;
    uint32 max_n = BLOCK_SIZE / sizeof(dirent_t);

    int free_index = -1;

    // 3. 扫描目录项：找空位 + 检查重名
    for (uint32 i = 0; i < max_n; i++) {
        if (ents[i].inode_num == INODE_NUM_UNUSED) {
            if (free_index < 0) {
                free_index = (int)i;
            }
            continue;
        }

        // 非空项，检查名字是否重复
        if (strncmp(ents[i].name, name, DIR_NAME_LEN) == 0) {
            // 重名，失败
            buf_release(bp);
            return BLOCK_SIZE;
        }
    }

    // 4. 没有空位：目录已满
    if (free_index < 0) {
        buf_release(bp);
        return BLOCK_SIZE;
    }
    // 5. 在 free_index 写入新目录项
    dirent_t *e = &ents[free_index];

    e->inode_num = inode_num;

    // 写名字：清零 + 拷贝 + 保证 '\0' 结尾
    memset(e->name, 0, DIR_NAME_LEN);
    uint64 len = kstrlen(name);
    if (len >= DIR_NAME_LEN) {
        len = DIR_NAME_LEN - 1;
    }
    for (uint64 j = 0; j < len; ++j) {
        e->name[j] = name[j];
    }
    e->name[len] = '\0';

    // 写回目录块
    buf_write(bp);
    buf_release(bp);

    // 6. 更新目录 inode 的 size
    uint32 entry_end_offset = (free_index + 1) * sizeof(dirent_t);
    if (entry_end_offset > pip->size) {
        pip->size = entry_end_offset;
        inode_rw(pip, true);
    }

    // 7. 返回该目录项的偏移量
    return free_index * sizeof(dirent_t);

}

// 在pip目录下删除一个目录项
// 成功返回这个目录项的inode_num
// 失败返回INODE_NUM_UNUSED
// ps: 调用者需持有pip的锁
uint16 dir_delete_entry(inode_t *pip, char *name)
{
    // 0. 基本检查
    // Debug 断言：必须已经持有 pip 的睡眠锁
    if (!sleeplock_holding(&pip->slk)) {
        panic("dir_add_entry: pip not locked");
    }
    if (pip == NULL || name == NULL) {
        return INODE_NUM_UNUSED;
    }
    // 必须是目录
    if (pip->type != FT_DIR) {
        return INODE_NUM_UNUSED;
    }
    // 目录还没有数据块，说明是空目录
    if (pip->addrs[0] == 0) {
        return INODE_NUM_UNUSED;
    }
    // 1. 读入目录块
    uint32 bno = pip->addrs[0];
    buf_t *bp = buf_read(bno);
    if (bp == NULL) {
        return INODE_NUM_UNUSED;
    }
    dirent_t *ents = (dirent_t *)bp->data;
    uint32 max_n = BLOCK_SIZE / sizeof(dirent_t);

    int del_index = -1;
    uint16 inum = INODE_NUM_UNUSED;
    // 2. 扫描目录项，找到名字为 name 的那一项并标记删除
    for (uint32 i = 0; i < max_n; i++) {
        if (ents[i].inode_num == INODE_NUM_UNUSED) {
            continue;   // 空目录项
        }

        // 比较名字（最多比较 DIR_NAME_LEN 个字符）
        if (strncmp(ents[i].name, name, DIR_NAME_LEN) == 0) {
            del_index = (int)i;
            inum = ents[i].inode_num;

            // 标记为未使用
            ents[i].inode_num = INODE_NUM_UNUSED;
            memset(ents[i].name, 0, DIR_NAME_LEN);

            break;
        }
    }
    // 没找到这个名字
    if (del_index < 0) {
        buf_release(bp);
        return INODE_NUM_UNUSED;
    }
    // 3. 删除成功，先写回目录块
    buf_write(bp);
    buf_release(bp);

    // 4. 重新计算目录的有效大小 size
    //    寻找最后一个非空目录项的位置
    int max_used = -1;
    for (int j = (int)max_n - 1; j >= 0; --j) {
        if (ents[j].inode_num != INODE_NUM_UNUSED) {
            max_used = j;
            break;
        }
    }

    uint32 new_size;
    if (max_used < 0) {
        // 目录现在完全空了
        new_size = 0;
    } else {
        new_size = (max_used + 1) * sizeof(dirent_t);
    }

    // 如果 size 有变化，更新 inode 并写回磁盘
    if (new_size != pip->size) {
        pip->size = new_size;
        inode_rw(pip, true);
    }

    // 5. 返回被删除目录项的 inode 号
    return inum;
}

// 把目录下的有效目录项复制到dst (dst区域长度为len)
// 返回读到的字节数 (sizeof(dirent_t)*n)
// 调用者需要持有pip的锁
uint32 dir_get_entries(inode_t* pip, uint32 len, void* dst, bool user)
{
    // 0. 基本参数检查
    if (pip == NULL || dst == NULL) {
        return 0;
    }
    if (len < sizeof(dirent_t)) {
        return 0;   // 缓冲区太小，连一个项都放不下
    }

    if (pip->type != FT_DIR) {
        return 0;   // 不是目录
    }

    if (pip->addrs[0] == 0 || pip->size == 0) {
        // 目录没有数据块或者逻辑大小为 0，当作空目录
        return 0;
    }

    // 1. 读出目录块
    uint32 bno = pip->addrs[0];
    buf_t *bp = buf_read(bno);
    if (bp == NULL) {
        return 0;
    }
    dirent_t *ents = (dirent_t *)bp->data;

    // 2. 计算目录项数量
    uint32 max_n = BLOCK_SIZE / sizeof(dirent_t);      // 一块里最多容纳的dirent数
    uint32 n_dir = pip->size / sizeof(dirent_t);       // 逻辑上的目录项数
    if (n_dir > max_n) {
        n_dir = max_n; // 保险：不要越界
    }

    // dst 最多能装多少个 dirent
    uint32 cap_n = len / sizeof(dirent_t);
    if (cap_n == 0) {
        buf_release(bp);
        return 0;
    }

    uint32 copied = 0;   // 已经拷出的有效目录项个数
    uint32 out_off = 0;  // dst 中已经使用的字节数

    // 3. 遍历目录项，过滤掉无效项并打包输出
    for (uint32 i = 0; i < n_dir && copied < cap_n; i++) {
        if (ents[i].inode_num == INODE_NUM_UNUSED) {
            continue;   // 空目录项，跳过
        }

        // 目标位置 = dst + out_off
        uint64 dst_addr = (uint64)dst + out_off;
        if (!user) {
            // 内核缓冲区，直接memmove
            memmove((void *)dst_addr, &ents[i], sizeof(dirent_t));
        } else {
            // 用户缓冲区，需要通过copyout之类的函数
            either_copyout(true, dst_addr, &ents[i],
                               sizeof(dirent_t)) ;
        }

        copied++;
        out_off += sizeof(dirent_t);
    }

    buf_release(bp);

    // 返回拷出的总字节数
    return copied * sizeof(dirent_t);
}

// 改变进程里存储的当前目录
// 成功返回0 失败返回-1
uint32 dir_change(char* path)
{

}

// 输出一个目录下的所有有效目录项
// for debug
// ps: 调用者需持有pip的锁
void dir_print(inode_t *pip)
{
    assert(sleeplock_holding(&pip->slk), "dir_print: lock");

    printf("\ninode_num = %d dirents:\n", pip->inode_num);

    dirent_t *de;
    buf_t *buf = buf_read(pip->addrs[0]);
    for (uint32 offset = 0; offset < BLOCK_SIZE; offset += sizeof(dirent_t))
    {
        de = (dirent_t *)(buf->data + offset);
        if (de->name[0] != 0)
            printf("inum = %d dirent = %s\n", de->inode_num, de->name);
    }
    buf_release(buf);
}

/*----------------------- 路径(一串目录和文件) -------------------------*/

// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
static char *skip_element(char *path, char *name)
{
    while(*path == '/') path++;
    if(*path == 0) return 0;

    char *s = path;
    while (*path != '/' && *path != 0)
        path++;

    int len = path - s;
    if (len >= DIR_NAME_LEN) {
        memmove(name, s, DIR_NAME_LEN);
    } else {
        memmove(name, s, len);
        name[len] = 0;
    }
    while (*path == '/')
        path++;
    return path;
}

// 查找路径path对应的inode (find_parent = false)
// 查找路径path对应的inode的父节点 (find_parent = true)
// 供两个上层函数使用
// 失败返回NULL
static inode_t* search_inode(char* path, char* name, bool find_parent)
{

}

// 找到path对应的inode
inode_t* path_to_inode(char* path)
{
    char name[DIR_NAME_LEN];
    return search_inode(path, name, false);
}

// 找到path对应的inode的父节点
// path最后的目录名放入name指向的空间
inode_t* path_to_pinode(char* path, char* name)
{
    return search_inode(path, name, true);
}

// 如果path对应的inode存在则返回inode
// 如果path对应的inode不存在则创建inode
// 失败返回NULL
inode_t* path_create_inode(char* path, uint16 type, uint16 major, uint16 minor)
{

}

// 文件链接(目录不能被链接)
// 本质是创建一个目录项, 这个目录项的inode_num是存在的而不用申请
// 成功返回0 失败返回-1
uint32 path_link(char* old_path, char* new_path)
{

}

// 检查一个unlink操作是否合理
// 调用者需要持有ip的锁
// 在path_unlink()中调用
static bool check_unlink(inode_t* ip)
{
    assert(sleeplock_holding(&ip->slk), "check_unlink: slk");

    uint8 tmp[sizeof(dirent_t) * 3];
    uint32 read_len;
    
    read_len = dir_get_entries(ip, sizeof(dirent_t) * 3, tmp, false);
    
    if(read_len == sizeof(dirent_t) * 3) {
        return false;
    } else if(read_len == sizeof(dirent_t) * 2) {
        return true;
    } else {
        panic("check_unlink: read_len");
        return false;
    }
}

// 文件删除链接
uint32 path_unlink(char* path)
{
    
}