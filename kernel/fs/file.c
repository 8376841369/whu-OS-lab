#include "fs/fs.h"
#include "fs/buf.h"
#include "fs/dir.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/file.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "lib/print.h"


// 设备列表(读写接口)
dev_t devlist[N_DEV];

// ftable + 保护它的锁
#define N_FILE 32
file_t ftable[N_FILE];
spinlock_t lk_ftable;

// ftable初始化 + devlist初始化
void file_init()
{
    spinlock_init(&lk_ftable, "ftable");

    for(int i = 0;i < N_FILE;i++)
    {
        ftable[i].type = FD_UNUSED;
        ftable[i].ref = 0;
        ftable[i].ip = NULL;
        ftable[i].offset = 0;
        ftable[i].major = 0;
        ftable[i].readable = false;
        ftable[i].writable = false; 
    }

    for(int i = 0 ;i < N_DEV;i++)
    {
        devlist[i].read = NULL;
        devlist[i].write = NULL;
    }
}

// alloc file_t in ftable
// 失败则panic
file_t* file_alloc()
{
    spinlock_acquire(&lk_ftable);

    for(int i =0;i<N_FILE;i++)
    {
        file_t *f = &ftable[i];
        // 约定：ref==0 代表空闲槽位（也可同时检查 type==FD_UNUSED）
        if (f->ref == 0) 
        {
            // 初始化这个 file 对象
            f->ref      = 1;
            f->type     = FD_UNUSED;   // 由 file_open/file_create_dev 等再设置为 FD_FILE/FD_DIR/FD_DEVICE/FD_PIPE
            f->readable = false;
            f->writable = false;
            f->major    = 0;
            f->offset   = 0;
            f->ip       = NULL;

            spinlock_release(&lk_ftable);
            return f;
        }
    }

    spinlock_release(&lk_ftable);
    panic("file_alloc: no free file");
    return NULL;
}

// 创建设备文件(供proczero创建console)
file_t* file_create_dev(char* path, uint16 major, uint16 minor)
{
    if (path == NULL || path[0] == '\0') {
        return NULL;
    }
    if (major >= N_DEV) {
        return NULL;
    }
    // 要求该 major 已注册读写接口（避免之后 NULL 函数指针）
    if (devlist[major].read == NULL && devlist[major].write == NULL) {
        // 没注册驱动也可以允许创建节点；但通常这意味着后续无法用
        // 这里按“更严格”处理：直接失败
        return NULL;
    }

    file_t * f = file_alloc();
    if(f == NULL)
    {
        return NULL;
    }
    inode_t* ip = path_create_inode(path,FT_DEVICE,major,minor);
    if(ip == NULL)
    {
         // 回滚 file_alloc：把 f 释放回空闲
        spinlock_acquire(&lk_ftable);
        f->ref = 0;
        f->type = FD_UNUSED;
        f->ip = NULL;
        f->offset = 0;
        f->major = 0;
        f->readable = false;
        f->writable = false;
        spinlock_release(&lk_ftable);
        return NULL;
    }
    // 3) 确认 inode 真的是设备，并把 major/minor 写回（防御 + 修正）
    inode_lock(ip);
    if (ip->type != FT_DEVICE) {
        inode_unlock_free(ip);

        // 回滚 file
        spinlock_acquire(&lk_ftable);
        f->ref = 0;
        f->type = FD_UNUSED;
        f->ip = NULL;
        f->offset = 0;
        f->major = 0;
        f->readable = false;
        f->writable = false;
        spinlock_release(&lk_ftable);

        return NULL;
    }

     // 如果该节点已存在但 major/minor 不一致，你可以选择：
    // 1) 直接失败；2) 覆盖更新。
    // 这里选择覆盖更新（更符合“创建 console 节点”场景）
    ip->major = major;
    ip->minor = minor;
    inode_rw(ip, true);

    inode_unlock(ip);

    // 4) 填充 file_t
    // 设备文件一般不维护 offset；读写走 devlist
    f->type     = FD_DEVICE;
    f->major    = major;
    f->ip       = ip;          // 持有 inode 引用（之后 file_close 里要 inode_free）
    f->offset   = 0;

    // console 通常可读可写；你也可以按需要只开一边
    f->readable = true;
    f->writable = true;

    return f;
}

// 打开一个文件
file_t* file_open(char* path, uint32 open_mode)
{
    if (!path || path[0] == '\0') return NULL;
    bool r = (open_mode & MODE_READ) != 0;
    bool w = (open_mode & MODE_WRITE) != 0;
    bool c = (open_mode & MODE_CREATE) != 0;
    if(!r && !w) return NULL;//至少要读或写一种模式

    inode_t* ip = NULL;

    if(c)
    {
        ip = path_create_inode(path, FT_FILE, 0, 0);
        if(ip == NULL)
        {
            return NULL;
        }
    }
    else
    {
        ip = path_to_inode(path);
        if(ip == NULL)
        {
            return NULL;
        }
    }

    // 锁住读 type/major/minor
    inode_lock(ip);
    uint16 t = ip->type;
    uint16 major = ip->major;
    uint16 minor = ip->minor;  // 你未来可能会用
    inode_unlock(ip);

    // 目录不允许以可写方式 open（建议）
    if (t == FT_DIR && w) {
        inode_free(ip);
        return NULL;
    }

    // 分配 file
    file_t* f = file_alloc();  // 失败会 panic
    if (!f) {
        inode_free(ip);
        return NULL;
    }

    // 填充
    f->readable = r;
    f->writable = w;
    f->offset   = 0;
    f->ip       = ip;

    if (t == FT_FILE) {
        f->type = FD_FILE;
        return f;
    }

    if (t == FT_DIR) {
        f->type = FD_DIR;
        return f;
    }

    if (t == FT_DEVICE) {
        if (major >= N_DEV) goto fail;
        // 可选：严格检查驱动存在
        if (r && devlist[major].read  == NULL) goto fail;
        if (w && devlist[major].write == NULL) goto fail;

        f->type  = FD_DEVICE;
        f->major = major;
        return f;
    }

fail:
    // 回滚：释放 file 槽位 + inode 引用
    // 建议封装成 file_close(f) 或 file_free(f)
    spinlock_acquire(&lk_ftable);
    f->ref = 0;
    f->type = FD_UNUSED;
    f->ip = NULL;
    f->offset = 0;
    f->major = 0;
    f->readable = false;
    f->writable = false;
    spinlock_release(&lk_ftable);

    inode_free(ip);
    return NULL;
}

// 释放一个file
void file_close(file_t* file)
{
    if (file == NULL) {
        return;
    }

    inode_t *ip = NULL;
    uint16 ftype = FD_UNUSED;

    spinlock_acquire(&lk_ftable);
    if(file->ref < 1)
    {
        spinlock_release(&lk_ftable);
        panic("file_close: ref < 1");
    }
    file->ref--;
    // 还有引用：仅减少 ref，不释放底层资源
    if (file->ref > 0) {
        spinlock_release(&lk_ftable);
        return;
    }

    ftype = file->type;
    ip = file->ip;

    file->type     = FD_UNUSED;
    file->readable = false;
    file->writable = false;
    file->major    = 0;
    file->offset   = 0;
    file->ip       = NULL;
    spinlock_release(&lk_ftable);

    // 3) 锁外释放底层资源
    switch (ftype) {
    case FD_FILE:
    case FD_DIR:
    case FD_DEVICE:
        if (ip) {
            // 释放对 inode 的引用（与你 inode_alloc/inode_dup 对应）
            inode_free(ip);
        }
        break;

    case FD_PIPE:
        // 如果你实现了 pipe，需要在这里调用 pipeclose(file->pipe, writable) 之类
        // 现在先留空
        break;

    case FD_UNUSED:
    default:
        // FD_UNUSED 正常不会走到这里；default 做防御
        break;
    }
}

// 文件内容读取
// 返回读取到的字节数
uint32 file_read(file_t* file, uint32 len, uint64 dst, bool user)
{
    if (file == NULL) {
        return 0;
    }
    if (len == 0) {
        return 0;
    }

    // 1) 权限检查
    if (!file->readable) {
        return 0;
    }

    // 2) 按类型分发
    switch (file->type) {
    case FD_FILE:
    case FD_DIR: {
        // 普通文件/目录：走 inode 数据读取
        if (file->ip == NULL) {
            return 0;
        }

        inode_lock(file->ip);
        uint32 n = inode_read_data(file->ip, file->offset, len, dst, user);
        file->offset += n;
        inode_unlock(file->ip);

        return n;
    }

    case FD_DEVICE: {
        // 设备文件：走 devlist[major].read
        uint16 major = file->major;
        if (major >= N_DEV) {
            return 0;
        }
        if (devlist[major].read == NULL) {
            return 0;
        }
        // 设备读一般不使用 file->offset
        return devlist[major].read(len, dst, user);
    }

    case FD_PIPE:
        // 你目前 file_t 里没有 pipe 指针，说明 pipe 还没接上
        // 先返回0或panic都行；这里选择返回0以免崩
        return 0;

    case FD_UNUSED:
    default:
        return 0;
    }
}

// 文件内容写入
// 返回写入的字节数
uint32 file_write(file_t* file, uint32 len, uint64 src, bool user)
{
    if (file == NULL) {
        
        return 0;
    }
    if (len == 0) {
        return 0;
    }

    // 1) 权限检查
    if (!file->writable) {
        return 0;
    }
  
    // 2) 按类型分发
    switch (file->type) {
    case FD_FILE: {
        if (file->ip == NULL) {
            return 0;
        }

        inode_lock(file->ip);
        uint32 n = inode_write_data(file->ip, file->offset, len, src, user);
        file->offset += n;
        inode_unlock(file->ip);

        return n;
    }

    case FD_DIR:
        // 一般不允许用 write() 修改目录（目录项通过 link/unlink/mkdir 操作）
        return 0;

    case FD_DEVICE: {
        uint16 major = file->major;
        if (major >= N_DEV) {
            return 0;
        }
        if (devlist[major].write == NULL) {
            return 0;
        }
        // 设备写一般不使用 file->offset
        return devlist[major].write(len, src, user);
    }

    case FD_PIPE:
        // 你当前 file_t 没有 pipe 指针，pipe 逻辑还没接上
        return 0;

    case FD_UNUSED:
    default:
        return 0;
    }
}

// flags 可能取值
#define LSEEK_SET 0  // file->offset = offset
#define LSEEK_ADD 1  // file->offset += offset
#define LSEEK_SUB 2  // file->offset -= offset

// 修改file->offset (只针对FD_FILE类型的文件)
uint32 file_lseek(file_t* file, uint32 offset, int flags)
{
    if (file == NULL) {
        return (uint32)-1;
    }

    // 只支持普通文件
    if (file->type != FD_FILE) {
        return (uint32)-1;
    }

    // 读写权限不影响 seek，一般只要 file 有效即可
    // 但 ip 不能为空
    if (file->ip == NULL) {
        return (uint32)-1;
    }

    uint32 new_off = 0;

    // 计算新的 offset（注意 ADD 溢出、SUB 下溢）
    switch (flags) {
    case LSEEK_SET:
        new_off = offset;
        break;

    case LSEEK_ADD:
        if (file->offset > 0xFFFFFFFFu - offset) { // 溢出检查
            return (uint32)-1;
        }
        new_off = file->offset + offset;
        break;

    case LSEEK_SUB:
        if (file->offset < offset) {               // 下溢检查
            return (uint32)-1;
        }
        new_off = file->offset - offset;
        break;

    default:
        return (uint32)-1;
    }
}

// file->ref++ with lock
file_t* file_dup(file_t* file)
{
    spinlock_acquire(&lk_ftable);
    assert(file->ref > 0, "file_dup: ref");
    file->ref++;
    spinlock_release(&lk_ftable);
    return file;
}

// 获取文件状态
int file_stat(file_t* file, uint64 addr)
{
    file_state_t state;
    if(file->type == FD_FILE || file->type == FD_DIR)
    {
        inode_lock(file->ip);
        state.type = file->ip->type;
        state.inode_num = file->ip->inode_num;
        state.nlink = file->ip->nlink;
        state.size = file->ip->size;
        inode_unlock(file->ip);

        uvm_copyout(myproc()->pgtbl, addr, (uint64)&state, sizeof(file_state_t));
    }
    return -1;
}

// 设备写：把用户/内核缓冲区的内容输出到串口
uint32 console_write(uint32 len, uint64 src, bool user_src)
{
    proc_t* p = myproc();
    uint8 ch;
    uint32 i;

    for (i = 0; i < len; i++) {
        if (user_src) {
            
            uvm_copyin(p->pgtbl, (uint64)&ch, src + i, 1);
        } else {
            ch = *(uint8*)(src + i);
        }
      
        uart_putc_sync(ch);
    }
    return i;
}

// 设备读：从串口读取字符放到用户/内核缓冲区
// 极简策略：
// - uart_getc_sync() 若无数据返回 <0，则直接返回已读字节数（可能是0）
// - 不阻塞（或者你想阻塞就 while 等到读到一个字符再返回）
uint32 console_read(uint32 len, uint64 dst, bool user_dst)
{
    proc_t* p = myproc();
    uint8 ch;
    uint32 i;

    for (i = 0; i < len; i++) {
        int c = uart_getc_sync();
        if (c < 0) {
            break;  // 没有输入了：直接返回（非阻塞）
        }
        ch = (uint8)c;

        if (user_dst) {
            uvm_copyout(p->pgtbl, dst + i, (uint64)&ch, 1);               
        } else {
            *(uint8*)(dst + i) = ch;
        }
    }
    return i;
}

void console_init()
{
    // 注册控制台设备读写接口
    devlist[DEV_CONSOLE].read  = console_read;
    devlist[DEV_CONSOLE].write = console_write;
  

}