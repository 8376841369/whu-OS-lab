#include "userlib.h"

// 成功返回argc 失败返回-1
int sys_exec(char* path, char** argv)
{
    return syscall(SYS_exec, path, argv);
}

// 成功返回新的堆顶 失败返回-1
uint64 sys_brk(uint64 new_heap_top)
{
    return syscall(SYS_brk, new_heap_top);
}

// 成功返回映射空间的起始地址, 失败返回-1
uint64 sys_mmap(uint64 start, uint32 len)
{
    return syscall(SYS_mmap, start, len);
}

// 成功返回0 失败返回-1
uint64 sys_munmap(uint64 start, uint32 len)
{
    return syscall(SYS_munmap, start, len);
}

// 父进程返回子进程pid 子进程返回0
int sys_fork()
{
    return syscall(SYS_fork);
}

// 成功返回子进程pid，失败返回-1
int sys_wait(void* addr)
{
    return syscall(SYS_wait, addr);
}

// 不返回
int sys_exit(int exit_state)
{
    return syscall(SYS_exit, exit_state);
}

// 成功返回0, 失败返回-1
int sys_sleep(uint32 seconds)
{
    return syscall(SYS_sleep, seconds);
}

// 成功返回fd 失败返回-1
int sys_open(char* path, uint32 open_mode)
{
    return syscall(SYS_open, path, open_mode);
}

// 成功返回0 失败返回-1
int sys_close(int fd)
{
    return syscall(SYS_close, fd);
}

// 成功返回字节数 失败返回0或-1
uint32 sys_read(int fd, uint32 len, void* addr)
{
    return syscall(SYS_read, fd, len, addr);
}

// 成功返回字节数 失败返回0或-1
uint32 sys_write(int fd, uint32 len, void* addr)
{
    return syscall(SYS_write, fd, len, addr);
}

// 成功返回新的偏移量, 失败返回-1
uint32 sys_lseek(int fd, uint32 offset, int flags)
{
    return syscall(SYS_lseek, fd, offset, flags);
}

// 成功返回 new_fd 失败返回 -1
int sys_dup(int fd)
{
    return syscall(SYS_dup, fd);
}

// 成功返回0 失败返回-1
int sys_fstat(int fd, fstat_t* state)
{
    return syscall(SYS_fstat, fd, state);
}

// 成功返回读取的字节数, 失败返回-1
uint32 sys_getdir(int fd, dirent_t* addr, uint32 len)
{
    return syscall(SYS_getdir, fd, addr, len);
}

// 成功返回0 失败返回-1
int sys_mkdir(char* path)
{
    return syscall(SYS_mkdir, path);
}

// 成功返回0 失败返回-1
int sys_chdir(char* path)
{
    return syscall(SYS_chdir, path);
}

// 成功返回0 失败返回-1
int sys_link(char* old_path, char* new_path)
{
    return syscall(SYS_link, old_path, new_path);
}

// 成功返回0 失败返回-1
int sys_unlink(char* path)
{
    return syscall(SYS_unlink, path);
}
