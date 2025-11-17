
// in initcode.c
#include "sys.h"

int main()
{
    int pid = syscall(SYS_fork);

    if(pid == 0) { // 子进程
        for(int i = 0; i < 100000000; i++);
        syscall(SYS_print, "child: hello\n");
        
    }else
    {
        syscall(SYS_print, "parent: hello\n");
      
    }
    while(1);
    return 0;
}