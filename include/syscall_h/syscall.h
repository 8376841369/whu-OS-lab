#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include "common.h"

// 系统调用主处理函数
#define DIR_PATH_LEN 128
#define ELF_MAXARGS 32

void syscall(void);

// 基于参数寄存器编号的读取

void arg_uint32(int n, uint32* ip);
void arg_uint64(int n, uint64* ip);
void arg_str(int n, char* buf, int maxlen);

#endif