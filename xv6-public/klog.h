// Kernel logging subsystem header
#ifndef KLOG_H
#define KLOG_H

#include "types.h"
#include "spinlock.h"

// Log entry structure (88 bytes)
struct klog_entry {
  uint seq;           // Global sequence number
  uint timestamp_hi;  // High 32 bits of TSC timestamp
  uint timestamp_lo;  // Low 32 bits of TSC timestamp
  uint cpu;           // CPU ID
  uint pid;           // Process ID (0 for kernel)
  uint level;         // Log level (DEBUG, INFO, WARN, ERROR)
  char msg[64];       // Null-terminated formatted message
};

#define KLOG_BUF_SIZE 256  // Per-CPU buffer size (power of 2)

// Rate limiter state per CPU (Token-Bucket algorithm)
struct klog_ratelimit {
  uint tokens;
  uint last_tsc;
  uint suppressed;
};

// Per-CPU log buffer
struct klog_cpu_buf {
  struct spinlock lock;
  struct klog_entry entries[KLOG_BUF_SIZE];
  uint head;          // Next write position
  uint dropped;       // Count of dropped entries due to overflow
  struct klog_ratelimit rlimit; // Token-bucket rate limiter
};

// Log levels
#define KLOG_DEBUG 0
#define KLOG_INFO  1
#define KLOG_WARN  2
#define KLOG_ERROR 3

// Subsystem Categories (Bitmasks)
#define KLOG_SUB_CORE 0x01
#define KLOG_SUB_PROC 0x02
#define KLOG_SUB_FS   0x04
#define KLOG_SUB_MEM  0x08
#define KLOG_SUB_ALL  0xFF

// Token Bucket Rate Limiter Constants
#define KLOG_RATE_BURST      15         // Max burst tokens
#define KLOG_REFILL_CYCLES   50000000   // Refill interval in TSC cycles (~25ms)

// Crash Dump Disk Persistence Constants
#define KLOG_CRASH_MAGIC 0x4B4C4F47     // 'KLOG'
#define KLOG_CRASH_BLOCK 995            // Dedicated disk block on fs.img

// Crash dump record (Fits within a 512-byte disk block)
struct klog_crash_dump {
  uint magic;
  char panic_msg[64];
  uint timestamp_hi;
  uint timestamp_lo;
  uint cpu;
  uint entry_count;
  struct klog_entry entries[4]; // 4 entries * 88 = 352B + header = 380B <= 512B
};

// Kernel logging functions
void klog_init(void);
void klog_printf(const char *fmt, ...);
void klog_printf_level(int level, const char *fmt, ...);
void klog_printf_sub(uint sub, int level, const char *fmt, ...);
int klog_snapshot(struct klog_entry *buf, int max_entries);
struct proc;
int klog_snapshot_user(struct proc *p, uint user_dst, int max_entries);
int klog_read_next(uint *last_seq, struct klog_entry *entry);
void klog_clear(void);
uint klog_get_dropped(void);

// Subsystem Mask Controls
void klog_set_mask(uint mask);
uint klog_get_mask(void);

// Crash Dump Routines
void klog_panic_dump(char *panic_msg);
int klog_read_crash_dump(struct klog_crash_dump *dst);

// Convenience macros
#define klog_debug(fmt, ...) klog_printf_level(KLOG_DEBUG, fmt, ##__VA_ARGS__)
#define klog_info(fmt, ...)  klog_printf_level(KLOG_INFO, fmt, ##__VA_ARGS__)
#define klog_warn(fmt, ...)  klog_printf_level(KLOG_WARN, fmt, ##__VA_ARGS__)
#define klog_error(fmt, ...) klog_printf_level(KLOG_ERROR, fmt, ##__VA_ARGS__)

#define klog_proc(fmt, ...) klog_printf_sub(KLOG_SUB_PROC, KLOG_INFO, fmt, ##__VA_ARGS__)
#define klog_fs(fmt, ...)   klog_printf_sub(KLOG_SUB_FS, KLOG_INFO, fmt, ##__VA_ARGS__)
#define klog_mem(fmt, ...)  klog_printf_sub(KLOG_SUB_MEM, KLOG_INFO, fmt, ##__VA_ARGS__)

#endif // KLOG_H
