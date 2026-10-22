#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "klog.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// Get kernel log snapshot
int
sys_getklog(void)
{
  int buf_addr;
  int max_entries;
  struct proc *curproc = myproc();
  uint uaddr, total_bytes;

  // Get arguments
  if(argint(0, &buf_addr) < 0 || argint(1, &max_entries) < 0)
    return -1;

  if(max_entries <= 0 || max_entries > 1024)
    return -1;

  uaddr = (uint)buf_addr;
  total_bytes = (uint)max_entries * sizeof(struct klog_entry);

  // Overflow-safe validation of user buffer address range (reject NULL and out of bounds)
  if(uaddr == 0 || uaddr >= curproc->sz || total_bytes > curproc->sz || 
     uaddr + total_bytes > curproc->sz || uaddr + total_bytes < uaddr)
    return -1;

  return klog_snapshot_user(curproc, uaddr, max_entries);
}

// Set runtime subsystem filter bitmask
int
sys_setklogmask(void)
{
  int mask;
  if(argint(0, &mask) < 0)
    return -1;
  klog_set_mask((uint)mask);
  return 0;
}

// Get current runtime subsystem filter bitmask
int
sys_getklogmask(void)
{
  return klog_get_mask();
}

// Read post-mortem crash dump from persistent disk block
int
sys_getcrashdump(void)
{
  int buf_addr;
  struct proc *curproc = myproc();
  struct klog_crash_dump dump;

  if(argint(0, &buf_addr) < 0)
    return -1;

  uint uaddr = (uint)buf_addr;
  uint size = sizeof(struct klog_crash_dump);

  if(uaddr == 0 || uaddr >= curproc->sz || size > curproc->sz || 
     uaddr + size > curproc->sz || uaddr + size < uaddr)
    return -1;

  if(klog_read_crash_dump(&dump) < 0)
    return -1; // No crash dump available on disk

  if(copyout(curproc->pgdir, uaddr, &dump, size) < 0)
    return -1;

  return 0;
}

