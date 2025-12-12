#ifndef _STRING_H__
#define _STRING_H__

#include "common.h"

void *memset(void *dst, int c, int64 n);

void *memcpy(void* dst, const void* src, uint64 n);

void* memmove(void *dst, const void *src, uint64 n);

unsigned long kstrlen(const char *s);

int strncmp(const char *s1, const char *s2, uint64 n);
#endif