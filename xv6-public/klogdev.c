// /dev/klog character device implementation
#include "types.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "mmu.h"
#include "proc.h"
#include "klog.h"

// Device state for streaming reads
static struct {
  struct spinlock lock;
  uint last_seq;  // Last sequence number read
} klogdev;

void
klogdev_init(void)
{
  initlock(&klogdev.lock, "klogdev");
  klogdev.last_seq = 0;
  
  // Register device
  devsw[KLOG].read = klogdev_read;
  devsw[KLOG].write = klogdev_write;
}

// Read from /dev/klog
// Fix Issue 1: Bounded stack entry (sizeof(struct klog_entry) == 88 B)
// instead of entries[64] (5,632 B) which overflowed the 4,096 B kernel stack.
// Fix Issue 2: Stream cursor tracking (last_seq) so reads advance without repeating.
int
klogdev_read(struct inode *ip, char *dst, int n)
{
  struct klog_entry entry;
  int copied = 0;
  struct proc *curproc = myproc();

  if(n < (int)sizeof(struct klog_entry))
    return 0;

  acquire(&klogdev.lock);
  while(copied + (int)sizeof(struct klog_entry) <= n){
    if(!klog_read_next(&klogdev.last_seq, &entry)){
      break; // No more unread log entries available
    }
    
    if(copyout(curproc->pgdir, (uint)dst + copied, &entry, 
               sizeof(struct klog_entry)) < 0){
      release(&klogdev.lock);
      return -1;
    }
    copied += sizeof(struct klog_entry);
  }
  release(&klogdev.lock);

  return copied;
}

// Write to /dev/klog (not supported)
int
klogdev_write(struct inode *ip, char *buf, int n)
{
  return -1;  // Read-only device
}
