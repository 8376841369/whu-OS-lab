#ifndef __USER_H__
#define __USER_H__

#include "sys.h"
#include "type.h"

// 支持 printf

#define va_start(ap, last) (__builtin_va_start(ap, last))
#define va_arg(ap, type) (__builtin_va_arg(ap, type))
#define va_end(ap) (__builtin_va_end(ap))
#define va_copy(d, s) (__builtin_va_copy(d, s))
typedef __builtin_va_list va_list;

// 标准输入输出

#define STD_OUT 0
#define STD_IN  1

// 文件的打开方式 

#define MODE_CREATE    0x1 // 文件不存在则创建
#define MODE_READ      0x2 // 读文件
#define MODE_WRITE     0x4 // 写文件

// 文件类型

#define FD_UNUSED      0
#define FD_DIR         1
#define FD_FILE        2
#define FD_DEVICE      3
#define FD_PIPE        4

// 支持LSEEK

#define LSEEK_SET 0  // file->offset = offset
#define LSEEK_ADD 1  // file->offset += offset
#define LSEEK_SUB 2  // file->offset -= offset

// 来自user_syscall.c

int sys_exec(char* path, char** argv);
uint64 sys_brk(uint64 new_heap_top);
uint64 sys_mmap(uint64 start, uint32 len);
uint64 sys_munmap(uint64 start, uint32 len);
int sys_fork();
int sys_wait(void* addr);
int sys_exit(int exit_state);
int sys_sleep(uint32 seconds);
int sys_open(char* path, uint32 open_mode);
int sys_close(int fd);
uint32 sys_read(int fd, uint32 len, void* addr);
uint32 sys_write(int fd, uint32 len, void* addr);
uint32 sys_lseek(int fd, uint32 offset, int flags);
int sys_dup(int fd);
int sys_fstat(int fd, fstat_t* state);
uint32 sys_getdir(int fd, dirent_t* addr, uint32 len);
int sys_mkdir(char* path);
int sys_chdir(char* path);
int sys_link(char* old_path, char* new_path);
int sys_unlink(char* path);

// 来自user_lib.c

void   _main();
uint32 stdout(char* str, uint32 len);
uint32 stdin(char* str, uint32 len);
void   memset(void* begin, uint8 data, uint32 n);
void   memmove(void* dst, const void* src, uint32 n);
int    strncmp(const char *p, const char *q, uint32 n);
int    strlen(const char *str);
void   printf(const char* fmt, ...);
void   print_dirents(dirent_t* dir, uint32 count);
void   print_filestate(fstat_t* file);

#endif