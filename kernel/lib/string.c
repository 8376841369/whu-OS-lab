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


/* 返回以 '\0' 结尾的C串长度（不含 '\0'） */
unsigned long kstrlen(const char *s) {
    const char *p = s;
    while (*p) {
        ++p;
    }
    return (unsigned long)(p - s);
}

void*
memmove(void *dst, const void *src, uint64 n)
{
  const char *s;
  char *d;

  if(n == 0)
    return dst;
  
  s = src;
  d = dst;
  if(s < d && s + n > d){
    s += n;
    d += n;
    while(n-- > 0)
      *--d = *--s;
  } else
    while(n-- > 0)
      *d++ = *s++;

  return dst;
}

int strncmp(const char *s1, const char *s2, uint64 n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;

    for (uint64 i = 0; i < n; ++i) {
        unsigned char c1 = p1[i];
        unsigned char c2 = p2[i];

        // 遇到 '\0' 或者字符不同，都该停止
        if (c1 != c2) {
            return (int)c1 - (int)c2;
        }
        if (c1 == '\0') {  // 说明 c1 == c2 == '\0'
            return 0;
        }
    }

    // 前 n 个字符都一样
    return 0;
}

int memcmp(const void *s1, const void *s2, uint64 n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;

    for (uint64 i = 0; i < n; ++i) {
        unsigned char a = p1[i];
        unsigned char b = p2[i];
        if (a != b) {
            return (int)a - (int)b;
        }
    }
    return 0;
}