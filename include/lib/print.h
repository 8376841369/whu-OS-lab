#ifndef __PRINT_H__
#define __PRINT_H__

#include "common.h"

void print_init(void);
void print_int(long long xx,int base,int sign);
void print_ptr(uint64);
void printf(const char* fmt, ...);
void panic(const char* warning);
void assert(bool condition, const char* warning);

#endif