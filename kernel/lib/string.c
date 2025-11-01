#include "lib/string.h"

void *memset(void *dst, int c, int64 n) {
    unsigned char *p = (unsigned char *)dst;
    unsigned char v = (unsigned char)c;  // 只取低8位
    for (int64 i = 0; i < n; ++i) {
        p[i] = v;
    }
    return dst;
}

void *memcpy(void* dst, const void* src, uint64 n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (uint64 i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dst;
}