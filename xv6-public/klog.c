// Kernel logging subsystem implementation
#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "klog.h"

// Per-CPU log buffers
static struct klog_cpu_buf cpu_logs[NCPU];

// Global runtime subsystem filter mask (default all enabled)
static uint global_log_mask = KLOG_SUB_ALL;

// Global sequence counter
static uint global_seq = 0;
static struct spinlock seq_lock;

// Get high-resolution timestamp (using TSC)
static void
get_timestamp(uint *hi, uint *lo)
{
  uint tsc_lo, tsc_hi;
  asm volatile("rdtsc" : "=a"(tsc_lo), "=d"(tsc_hi));
  *hi = tsc_hi;
  *lo = tsc_lo;
}

// Get next sequence number using atomic instruction (eliminates global lock bottleneck)
static uint
next_seq(void)
{
  return __sync_fetch_and_add(&global_seq, 1);
}

// Initialize kernel logging subsystem
void
klog_init(void)
{
  int i;
  
  initlock(&seq_lock, "klog_seq");
  
  for(i = 0; i < NCPU; i++){
    initlock(&cpu_logs[i].lock, "klog_cpu");
    cpu_logs[i].head = 0;
    cpu_logs[i].dropped = 0;
    cpu_logs[i].rlimit.tokens = KLOG_RATE_BURST;
    cpu_logs[i].rlimit.last_tsc = 0;
    cpu_logs[i].rlimit.suppressed = 0;
  }
  
  klog_printf("klog: logging subsystem initialized");
}

// Helper: format integer
static int
snprintf_int(char *buf, int size, int val)
{
  char tmp[16];
  int i = 0, j = 0;
  int neg = 0;
  
  if(size <= 0)
    return 0;
  
  if(val < 0){
    neg = 1;
    val = -val;
  }
  
  if(val == 0){
    tmp[i++] = '0';
  } else {
    while(val > 0 && i < sizeof(tmp)){
      tmp[i++] = '0' + (val % 10);
      val /= 10;
    }
  }
  
  if(neg && j < size-1)
    buf[j++] = '-';
  
  while(i > 0 && j < size-1)
    buf[j++] = tmp[--i];
  
  buf[j] = 0;
  return j;
}

// Helper: format hex
static int
snprintf_hex(char *buf, int size, uint val)
{
  char tmp[16];
  int i = 0, j = 0;
  static char digits[] = "0123456789abcdef";
  
  if(size <= 2)
    return 0;
  
  buf[j++] = '0';
  buf[j++] = 'x';
  
  if(val == 0){
    tmp[i++] = '0';
  } else {
    while(val > 0 && i < sizeof(tmp)){
      tmp[i++] = digits[val & 0xf];
      val >>= 4;
    }
  }
  
  while(i > 0 && j < size-1)
    buf[j++] = tmp[--i];
  
  buf[j] = 0;
  return j;
}

// Internal logging function with subsystem and level
static void
klog_printf_internal(uint sub, int level, const char *fmt, uint *ap)
{
  int cpu_id;
  struct klog_cpu_buf *log;
  struct klog_entry *entry;
  uint idx;
  int pid = 0;
  char buf[64];
  int i;
  char *s;
  uint tsc_hi, tsc_lo;

  // Filter out disabled subsystems dynamically
  if(!(global_log_mask & sub))
    return;

  // Get current CPU
  pushcli();
  cpu_id = cpuid();
  if(cpu_id < 0 || cpu_id >= NCPU){
    popcli();
    return;
  }
  
  log = &cpu_logs[cpu_id];
  
  // Get current timestamp
  get_timestamp(&tsc_hi, &tsc_lo);

  // Token-Bucket Rate Limiter (KLOG_ERROR bypasses rate limiting)
  if(tsc_lo - log->rlimit.last_tsc > KLOG_REFILL_CYCLES){
    log->rlimit.tokens = KLOG_RATE_BURST;
    log->rlimit.last_tsc = tsc_lo;
  }

  if(level != KLOG_ERROR && log->rlimit.tokens == 0){
    log->rlimit.suppressed++;
    popcli();
    return; // Rate limited, skip formatting & insertion
  }

  if(log->rlimit.tokens > 0)
    log->rlimit.tokens--;

  // Get current process ID if available
  if(myproc())
    pid = myproc()->pid;
  
  // Format the message into buffer
  i = 0;
  for(; *fmt && i < sizeof(buf)-1; fmt++){
    if(*fmt != '%'){
      buf[i++] = *fmt;
      continue;
    }
    fmt++;
    if(*fmt == 0)
      break;
    switch(*fmt){
    case 'd':
      i += snprintf_int(buf + i, sizeof(buf) - i, *ap++);
      break;
    case 'x':
      i += snprintf_hex(buf + i, sizeof(buf) - i, *ap++);
      break;
    case 's':
      s = (char*)*ap++;
      if(s == 0)
        s = "(null)";
      for(; *s && i < sizeof(buf)-1; s++)
        buf[i++] = *s;
      break;
    case '%':
      if(i < sizeof(buf)-1)
        buf[i++] = '%';
      break;
    default:
      if(i < sizeof(buf)-2){
        buf[i++] = '%';
        buf[i++] = *fmt;
      } else if(i < sizeof(buf)-1){
        buf[i++] = *fmt;
      }
      break;
    }
  }
  if(i >= sizeof(buf))
    i = sizeof(buf) - 1;
  buf[i] = 0;
  
  // Add entry to log buffer
  acquire(&log->lock);
  
  // Track dropped/overwritten entries accurately
  if(log->head >= KLOG_BUF_SIZE){
    log->dropped++;
  }
  
  idx = log->head % KLOG_BUF_SIZE;
  entry = &log->entries[idx];
  
  entry->seq = next_seq();
  entry->timestamp_hi = tsc_hi;
  entry->timestamp_lo = tsc_lo;
  entry->cpu = cpu_id;
  entry->pid = pid;
  entry->level = level;
  
  // Copy message
  for(i = 0; i < sizeof(entry->msg)-1 && buf[i]; i++)
    entry->msg[i] = buf[i];
  entry->msg[i] = 0;
  
  log->head++;
  
  release(&log->lock);
  popcli();
}

// Log a formatted message (default CORE subsystem, INFO level)
void
klog_printf(const char *fmt, ...)
{
  uint *ap = (uint*)(void*)&fmt + 1;
  klog_printf_internal(KLOG_SUB_CORE, KLOG_INFO, fmt, ap);
}

// Log a formatted message with specific level
void
klog_printf_level(int level, const char *fmt, ...)
{
  uint *ap = (uint*)(void*)&fmt + 1;
  klog_printf_internal(KLOG_SUB_CORE, level, fmt, ap);
}

// Log with subsystem tag and specific level
void
klog_printf_sub(uint sub, int level, const char *fmt, ...)
{
  uint *ap = (uint*)(void*)&fmt + 1;
  klog_printf_internal(sub, level, fmt, ap);
}

// Snapshot all logs into user buffer - returns LAST (most recent) entries
// Corrected to handle sequence gaps from independent per-CPU ring overwrites
int
klog_snapshot(struct klog_entry *buf, int max_entries)
{
  int cpu_id, count, total = 0;
  struct klog_cpu_buf *log;
  uint start[NCPU], end[NCPU], cur[NCPU];
  int has_entry[NCPU];

  if(max_entries <= 0 || buf == 0)
    return 0;

  // 1. Determine valid ranges for each CPU
  for(cpu_id = 0; cpu_id < NCPU; cpu_id++){
    log = &cpu_logs[cpu_id];
    acquire(&log->lock);
    
    if(log->head > KLOG_BUF_SIZE){
      start[cpu_id] = log->head - KLOG_BUF_SIZE;
      end[cpu_id] = log->head;
      total += KLOG_BUF_SIZE;
    } else {
      start[cpu_id] = 0;
      end[cpu_id] = log->head;
      total += log->head;
    }
    
    cur[cpu_id] = end[cpu_id];
    has_entry[cpu_id] = (cur[cpu_id] > start[cpu_id]);
    release(&log->lock);
  }

  int target = total < max_entries ? total : max_entries;
  if(target == 0)
    return 0;

  // 2. Select newest entries working backwards from the newest in each CPU
  for(count = 0; count < target; count++){
    int best_cpu = -1;
    uint max_seq = 0;

    for(cpu_id = 0; cpu_id < NCPU; cpu_id++){
      if(has_entry[cpu_id]){
        log = &cpu_logs[cpu_id];
        acquire(&log->lock);
        uint idx = (cur[cpu_id] - 1) % KLOG_BUF_SIZE;
        uint s = log->entries[idx].seq;
        release(&log->lock);

        if(best_cpu == -1 || s > max_seq){
          max_seq = s;
          best_cpu = cpu_id;
        }
      }
    }

    if(best_cpu == -1)
      break;

    // Copy newest uncollected entry into our reverse slot so result is ascending
    log = &cpu_logs[best_cpu];
    acquire(&log->lock);
    uint idx = (cur[best_cpu] - 1) % KLOG_BUF_SIZE;
    buf[target - 1 - count] = log->entries[idx];
    release(&log->lock);

    cur[best_cpu]--;
    if(cur[best_cpu] <= start[best_cpu])
      has_entry[best_cpu] = 0;
  }

  return target;
}

// Snapshot newest max_entries directly to user space without page-size limits or large allocations
int
klog_snapshot_user(struct proc *p, uint user_dst, int max_entries)
{
  int cpu_id, count, total = 0;
  struct klog_cpu_buf *log;
  uint start[NCPU], end[NCPU], cur[NCPU];
  int has_entry[NCPU];
  struct klog_entry entry;

  if(max_entries <= 0 || p == 0)
    return 0;

  // 1. Determine valid ranges for each CPU
  for(cpu_id = 0; cpu_id < NCPU; cpu_id++){
    log = &cpu_logs[cpu_id];
    acquire(&log->lock);
    
    if(log->head > KLOG_BUF_SIZE){
      start[cpu_id] = log->head - KLOG_BUF_SIZE;
      end[cpu_id] = log->head;
      total += KLOG_BUF_SIZE;
    } else {
      start[cpu_id] = 0;
      end[cpu_id] = log->head;
      total += log->head;
    }
    
    cur[cpu_id] = end[cpu_id];
    has_entry[cpu_id] = (cur[cpu_id] > start[cpu_id]);
    release(&log->lock);
  }

  int target = total < max_entries ? total : max_entries;
  if(target == 0)
    return 0;

  // 2. Select newest entries working backwards from the newest in each CPU
  for(count = 0; count < target; count++){
    int best_cpu = -1;
    uint max_seq = 0;

    for(cpu_id = 0; cpu_id < NCPU; cpu_id++){
      if(has_entry[cpu_id]){
        log = &cpu_logs[cpu_id];
        acquire(&log->lock);
        uint idx = (cur[cpu_id] - 1) % KLOG_BUF_SIZE;
        uint s = log->entries[idx].seq;
        release(&log->lock);

        if(best_cpu == -1 || s > max_seq){
          max_seq = s;
          best_cpu = cpu_id;
        }
      }
    }

    if(best_cpu == -1)
      break;

    // Copy newest uncollected entry from kernel to local stack under lock
    log = &cpu_logs[best_cpu];
    acquire(&log->lock);
    uint idx = (cur[best_cpu] - 1) % KLOG_BUF_SIZE;
    entry = log->entries[idx];
    release(&log->lock);

    cur[best_cpu]--;
    if(cur[best_cpu] <= start[best_cpu])
      has_entry[best_cpu] = 0;

    // Transfer safely to user address space without holding any spinlock
    uint offset = (target - 1 - count) * sizeof(struct klog_entry);
    if(copyout(p->pgdir, user_dst + offset, &entry, sizeof(entry)) < 0)
      return -1;
  }

  return target;
}

// Read next unread log entry after *last_seq across all CPUs.
// Used for cursor-based streaming reads from /dev/klog.
int
klog_read_next(uint *last_seq, struct klog_entry *entry)
{
  int cpu_id;
  int found_cpu = -1;
  uint found_idx = 0;
  uint min_qualifying_seq = 0xFFFFFFFF;
  struct klog_cpu_buf *log;
  uint start, end, i, idx;

  for(cpu_id = 0; cpu_id < NCPU; cpu_id++){
    log = &cpu_logs[cpu_id];
    acquire(&log->lock);

    if(log->head > KLOG_BUF_SIZE){
      start = log->head - KLOG_BUF_SIZE;
      end = log->head;
    } else {
      start = 0;
      end = log->head;
    }

    for(i = start; i < end; i++){
      idx = i % KLOG_BUF_SIZE;
      if(log->entries[idx].seq > *last_seq){
        if(log->entries[idx].seq < min_qualifying_seq){
          min_qualifying_seq = log->entries[idx].seq;
          found_cpu = cpu_id;
          found_idx = idx;
        }
        break; // Within each CPU, entries are monotonic; first match is lowest seq for this CPU
      }
    }

    release(&log->lock);
  }

  if(found_cpu >= 0){
    log = &cpu_logs[found_cpu];
    acquire(&log->lock);
    *entry = log->entries[found_idx];
    *last_seq = entry->seq;
    release(&log->lock);
    return 1;
  }

  return 0;
}

// Clear all logs
void
klog_clear(void)
{
  int cpu_id;
  struct klog_cpu_buf *log;
  
  for(cpu_id = 0; cpu_id < NCPU; cpu_id++){
    log = &cpu_logs[cpu_id];
    acquire(&log->lock);
    log->head = 0;
    log->dropped = 0;
    release(&log->lock);
  }
}

// Get total dropped count
uint
klog_get_dropped(void)
{
  int cpu_id;
  uint total = 0;
  struct klog_cpu_buf *log;
  
  for(cpu_id = 0; cpu_id < NCPU; cpu_id++){
    log = &cpu_logs[cpu_id];
    acquire(&log->lock);
    total += log->dropped;
    release(&log->lock);
  }
  
  return total;
}

// Subsystem filter mask controls
void
klog_set_mask(uint mask)
{
  global_log_mask = mask;
}

uint
klog_get_mask(void)
{
  return global_log_mask;
}

// Synchronous crash dump to disk sector (works under panic / cli)
void
klog_panic_dump(char *panic_msg)
{
  struct klog_crash_dump dump;
  int i;
  uint tsc_hi, tsc_lo;

  dump.magic = KLOG_CRASH_MAGIC;
  dump.cpu = cpuid();
  get_timestamp(&tsc_hi, &tsc_lo);
  dump.timestamp_hi = tsc_hi;
  dump.timestamp_lo = tsc_lo;

  // Copy panic message safely
  for(i = 0; i < sizeof(dump.panic_msg)-1 && panic_msg && panic_msg[i]; i++)
    dump.panic_msg[i] = panic_msg[i];
  dump.panic_msg[i] = 0;

  // Extract the latest 4 entries from current CPU buffer
  struct klog_cpu_buf *log = &cpu_logs[dump.cpu];
  uint head = log->head;
  uint count = 0;
  uint start = (head > 4) ? head - 4 : 0;

  for(uint idx = start; idx < head && count < 4; idx++){
    dump.entries[count++] = log->entries[idx % KLOG_BUF_SIZE];
  }
  dump.entry_count = count;

  // Synchronous PIO write to disk 1 (fs.img) at block KLOG_CRASH_BLOCK
  ide_panic_write_block(1, KLOG_CRASH_BLOCK, &dump);
  cprintf("[klog] Synced post-mortem crash dump to disk block %d\n", KLOG_CRASH_BLOCK);
}

// Read crash dump from disk
int
klog_read_crash_dump(struct klog_crash_dump *dst)
{
  char buf[BSIZE];
  ide_sync_read_block(1, KLOG_CRASH_BLOCK, buf);
  struct klog_crash_dump *d = (struct klog_crash_dump*)buf;
  if(d->magic != KLOG_CRASH_MAGIC)
    return -1; // No valid crash dump found
  *dst = *d;
  return 0;
}

