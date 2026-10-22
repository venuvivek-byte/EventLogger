// Test program for kernel logging system
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

void
test_getklog(void)
{
  struct klog_entry *entries;
  int count, i;
  
  printf(1, "Testing getklog() syscall...\n");
  
  // Allocate buffer on heap
  entries = (struct klog_entry*)malloc(32 * sizeof(struct klog_entry));
  if(entries == 0){
    printf(2, "ERROR: malloc failed\n");
    return;
  }
  
  // Trigger some kernel activity
  fork();
  wait();
  
  // Get logs
  count = getklog(entries, 32);
  
  if(count < 0){
    printf(2, "ERROR: getklog() failed\n");
    free(entries);
    return;
  }
  
  printf(1, "Retrieved %d log entries:\n", count);
  for(i = 0; i < count && i < 10; i++){
    printf(1, "  [%d] CPU%d PID%d: %s\n",
           entries[i].seq, entries[i].cpu, entries[i].pid, entries[i].msg);
  }
  if(count > 10)
    printf(1, "  ... and %d more entries\n", count - 10);
  
  free(entries);
}

void
test_klog_device(void)
{
  int fd;
  struct klog_entry entry;
  int n, count = 0;
  
  printf(1, "\nTesting /dev/klog device...\n");
  
  // Create the device node if it doesn't exist
  mknod("klog", 2, KLOG);
  
  fd = open("klog", O_RDONLY);
  if(fd < 0){
    printf(2, "ERROR: Cannot open /dev/klog\n");
    return;
  }
  
  int prev_seq = -1;
  // Read some entries and verify cursor progression
  while(count < 5 && (n = read(fd, &entry, sizeof(entry))) == sizeof(entry)){
    printf(1, "  [%d] CPU%d PID%d: %s\n",
           entry.seq, entry.cpu, entry.pid, entry.msg);
    if(prev_seq != -1 && entry.seq <= prev_seq){
      printf(2, "ASSERTION FAILED: duplicate or non-advancing sequence %d <= %d\n",
             entry.seq, prev_seq);
      exit();
    }
    prev_seq = entry.seq;
    count++;
  }
  
  close(fd);
  printf(1, "Read %d entries from device with verified cursor advancement\n", count);
}

int
main(int argc, char *argv[])
{
  printf(1, "=== Kernel Logging System Test ===\n\n");
  
  test_getklog();
  test_klog_device();
  
  printf(1, "\n=== Test Complete ===\n");
  exit();
}
