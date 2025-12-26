#include "userlib.h"

// 所有磁盘里.c文件的main函数的外壳
void _main()
{
    extern int main();
    int exit_state = main();
    sys_exit(exit_state);
}

// 标准输出
uint32 stdout(char* str, uint32 len)
{
    return sys_write(STD_OUT, len, str);
}

// 标准输入
uint32 stdin(char* str, uint32 len)
{
    return sys_read(STD_IN, len, str);
}

// 从begin开始对连续n个字节赋值data
void memset(void *begin, uint8 data, uint32 n)
{
  uint8 *L = (uint8 *)begin;
  for (uint32 i = 0; i < n; i++)
    L[i] = data;
}

// copy
void memmove(void *dst, const void *src, uint32 n)
{
  const char *s = src;
  char *d = dst;
  while (n--)
  {
    *d = *s;
    d++;
    s++;
  }
}

// 字符串p的前n个字符与q做比较
// 按照ASCII码大小逐个比较
// 相同返回0 大于或小于返回正数或负数
int strncmp(const char *p, const char *q, uint32 n)
{
  while (n > 0 && *p && *p == *q)
    n--, p++, q++;
  if (n == 0)
    return 0;
  return (uint8)*p - (uint8)*q;
}

int strlen(const char *str)
{
  int i = 0;
  for (i = 0; str[i] != '\0'; i++)
    ;
  return i;
}

// 下面的函数用于支持printf

static char digits[] = "0123456789abcdef";

static void printint(int xx, int base, int sign)
{
    char buf[16 + 1];
    int i;
    uint32 x;

    if (sign && (sign = xx < 0))
        x = -xx;
    else
        x = xx;

    buf[16] = 0;
    i = 15;
    do
    {
        buf[i--] = digits[x % base];
    } while ((x /= base) != 0);

    if (sign)
        buf[i--] = '-';
    i++;
    if (i < 0)
        stdout("printint error", 14);
    stdout(buf + i, 16 - i);
}

static void printptr(uint64 x)
{
    int i = 0, j;
    char buf[32 + 1];
    buf[i++] = '0';
    buf[i++] = 'x';
    for (j = 0; j < (sizeof(uint64) * 2); j++, x <<= 4)
        buf[i++] = digits[x >> (sizeof(uint64) * 8 - 4)];
    buf[i] = 0;
    stdout(buf, i);
}

// 标准输出
void printf(const char *fmt, ...)
{
    va_list ap;
    int l = 0;
    char *a, *z, *s = (char *)fmt;

    va_start(ap, fmt);
    for (;;)
    {
        if (!*s)
            break;
        for (a = s; *s && *s != '%'; s++)
            ;
        for (z = s; s[0] == '%' && s[1] == '%'; z++, s += 2)
            ;
        l = z - a;
        stdout(a, l);
        if (l)
            continue;
        if (s[1] == 0)
            break;
        switch (s[1])
        {
        case 'd':
            printint(va_arg(ap, int), 10, 1);
            break;
        case 'x':
            printint(va_arg(ap, int), 16, 1);
            break;
        case 'p':
            printptr(va_arg(ap, uint64));
            break;
        case 's':
            if ((a = va_arg(ap, char *)) == 0)
                a = "(null)";
            l = strlen(a);
            stdout(a, l);
            break;
        default:
            stdout("%", 1);
            stdout(s + 1, 1);
            break;
        }
        s += 2;
    }
    va_end(ap);
}

void print_dirents(dirent_t* dir, uint32 count)
{
    printf("dirents information:\n");
    for(uint32 i = 0; i < count; i++)
        printf("inum = %d, dirname = %s\n",dir[i].inode_num, dir[i].name);
    printf("\n");
}

static char* file_type[] = {
    "UNUSED",
    "DIRECTOR",
    "FILE",
    "DEVICE",
    "PIPE",
};

void print_filestate(fstat_t* file)
{
    printf("file state:\n");
    printf("inum = %d ", (uint32)(file->inode_num));
    printf("nlink = %d ", (uint32)(file->nlink));
    printf("size = %d ", file->size);
    printf("type = %s\n", file_type[file->type]);
}