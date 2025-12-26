#ifndef __TYPE_H__
#define __TYPE_H__

// 类型定义

typedef char                   int8;
typedef short                  int16;
typedef int                    int32;
typedef long long              int64;
typedef unsigned char          uint8; 
typedef unsigned short         uint16;
typedef unsigned int           uint32;
typedef unsigned long long     uint64;

typedef unsigned long long         reg; 
typedef enum {false = 0, true = 1} bool;

#ifndef NULL
#define NULL ((void*)0)
#endif

// 目录定义
typedef struct dirent {
    uint16 inode_num;
    char name[30];
} dirent_t;

// 文件状态定义
typedef struct file_state {
    uint16 type;
    uint16 inode_num;
    uint16 nlink;
    uint32 size;
} fstat_t;

#endif