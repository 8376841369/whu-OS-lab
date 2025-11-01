#include "proc/cpu.h"
#include "riscv.h"

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


proc_t* myproc(void)
{
    push_off();
    struct cpu* c = mycpu();
    struct proc* p = c->proc;
    pop_off();
    return p;
}