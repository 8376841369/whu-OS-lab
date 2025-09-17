//#include "proc/cpu.h"
#include "riscv.h"
#include "proc/proc.h"

static cpu_t cpus[NCPU];

cpu_t* mycpu(void)
{
    int id = mycpuid();
    struct cpu *t = &cpus[id];
    return t;
}

int mycpuid(void) 
{
    int id = r_tp();
    return id;
}
