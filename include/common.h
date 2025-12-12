// 这个头文件通常认为其他.h文件都应该include
#ifndef __COMMON_H__
#define __COMMON_H__

// 类型定义

typedef char                   int8;
typedef short                  int16;
typedef int                    int32;
typedef long long              int64;
typedef unsigned char          uint8; 
typedef unsigned short         uint16;
typedef unsigned int           uint32;
typedef unsigned long long     uint64;
typedef unsigned long          uintptr_t;

typedef unsigned long long         reg; 
typedef enum {false = 0, true = 1} bool;

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - (unsigned long)(&((type *)0)->member)))


#ifndef NULL
#define NULL ((void*)0)
#endif

#define NCPU 2
#define PAGESIZE 4096
#define BLOCK_NUM_UNUSED 0xFFFFFFFF

#endif