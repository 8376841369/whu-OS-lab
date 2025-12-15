#include "lib/lock.h"
#include "lib/print.h"
#include  "proc/proc.h"
#include  "proc/cpu.h"


void
sleeplock_init(struct sleeplock *lk, char *name)
{
  spinlock_init(&lk->lk, "sleep lock");
  lk->name = name;
  lk->locked = 0;
  lk->pid = 0;
}

void
acquiresleep(struct sleeplock *lk)
{
  spinlock_acquire(&lk->lk);
  

  while (lk->locked) {
    proc_sleep(lk, &lk->lk);
  }
  lk->locked = 1;
  lk->pid = myproc()->pid;
  spinlock_release(&lk->lk);
}

void
releasesleep(struct sleeplock *lk)
{
  spinlock_acquire(&lk->lk);
  lk->locked = 0;
  lk->pid = 0;
  proc_wakeup(lk);
  spinlock_release(&lk->lk);
}

int
sleeplock_holding(struct sleeplock *lk)
{
  int r;
  
  spinlock_acquire(&lk->lk);
  r = lk->locked && (lk->pid == myproc()->pid);
  spinlock_release(&lk->lk);
  return r;
}