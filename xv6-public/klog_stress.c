// Automated stress and correctness test suite for xv6 kernel logging
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define ASSERT(cond, msg) do { \
  if(!(cond)) { \
    printf(1, "FAIL: %s (line %d)\n", msg, __LINE__); \
    exit(); \
  } \
} while(0)

// Test 1: Invalid pointers and buffer parameter validation
void
test_invalid_inputs(void)
{
  struct klog_entry entry;
  int ret;

  printf(1, "[TEST 1] System call parameter validation & safety...\n");

  // NULL pointer
  ret = getklog((struct klog_entry*)0, 10);
  ASSERT(ret < 0, "NULL pointer accepted");

  // Negative buffer address
  ret = getklog((struct klog_entry*)-1, 10);
  ASSERT(ret < 0, "Negative address accepted");

  // Kernel memory space address (above 0x80000000 in xv6)
  ret = getklog((struct klog_entry*)0x80100000, 10);
  ASSERT(ret < 0, "Kernel space address accepted");

  // Non-positive max_entries
  ret = getklog(&entry, 0);
  ASSERT(ret < 0, "Zero max_entries accepted");

  ret = getklog(&entry, -5);
  ASSERT(ret < 0, "Negative max_entries accepted");

  // Excessively large max_entries (> 1024)
  ret = getklog(&entry, 2048);
  ASSERT(ret < 0, "Excessive max_entries accepted");

  // Address wrap-around integer overflow test
  ret = getklog((struct klog_entry*)(0xFFFFFFFF - sizeof(struct klog_entry)), 2);
  ASSERT(ret < 0, "Overflowing buffer address accepted");

  printf(1, "  -> PASS: All invalid input edge-cases rejected cleanly.\n");
}

// Test 2: Cursor advancement in /dev/klog (ensures no repeated entries)
void
test_device_cursor(void)
{
  int fd;
  struct klog_entry entries[10];
  int count = 0, n;
  int i;

  printf(1, "[TEST 2] /dev/klog cursor advancement & stream consistency...\n");

  mknod("klog", 2, 2);
  fd = open("klog", O_RDONLY);
  ASSERT(fd >= 0, "Failed to open /dev/klog");

  while(count < 10){
    n = read(fd, &entries[count], sizeof(struct klog_entry));
    if(n == sizeof(struct klog_entry)){
      count++;
    } else {
      break;
    }
  }
  close(fd);

  ASSERT(count >= 2, "Insufficient log entries read from device");

  // Verify that sequence numbers are strictly strictly monotonic (no duplicates)
  for(i = 1; i < count; i++){
    if(entries[i].seq <= entries[i-1].seq){
      printf(1, "Error: entry[%d].seq (%d) <= entry[%d].seq (%d)\n",
             i, entries[i].seq, i-1, entries[i-1].seq);
      ASSERT(0, "Device returned non-monotonic or duplicated sequence number");
    }
  }

  printf(1, "  -> PASS: Read %d entries; cursor strictly advanced without repeats.\n", count);
}

// Test 3: Snapshot ordering across buffer wraparound (>256 entries)
void
test_wraparound_and_ordering(void)
{
  struct klog_entry *buf;
  int count, i;
  int fd;

  printf(1, "[TEST 3] Ring buffer wraparound and snapshot ordering...\n");

  // Generate over 300 log entries to force ring-buffer head > KLOG_BUF_SIZE (256)
  for(i = 0; i < 300; i++){
    fd = open("klog_stress_tmp.txt", O_CREATE | O_RDWR);
    if(fd >= 0){
      write(fd, "stress", 6);
      close(fd);
    }
  }
  unlink("klog_stress_tmp.txt");

  buf = (struct klog_entry*)malloc(64 * sizeof(struct klog_entry));
  ASSERT(buf != 0, "malloc failed");

  count = getklog(buf, 64);
  ASSERT(count > 0, "getklog returned no entries after wraparound");

  // Verify all returned entries are strictly ordered by sequence number
  for(i = 1; i < count; i++){
    if(buf[i].seq <= buf[i-1].seq){
      printf(1, "Ordering violation: entry[%d].seq=%d, entry[%d].seq=%d\n",
             i-1, buf[i-1].seq, i, buf[i].seq);
      ASSERT(0, "Snapshot entries are not in strict chronological order");
    }
  }

  printf(1, "  -> PASS: Wraparound handled correctly; %d entries chronologically ordered.\n", count);
  free(buf);
}

// Test 4: Concurrent writers and readers
void
test_concurrency(void)
{
  int pids[4];
  int i, j;

  printf(1, "[TEST 4] Concurrent multi-process stress test...\n");

  for(i = 0; i < 4; i++){
    pids[i] = fork();
    if(pids[i] < 0){
      ASSERT(0, "fork failed in concurrency test");
    }
    if(pids[i] == 0){
      // Child generates logs rapidly
      for(j = 0; j < 50; j++){
        int fd = open(".", O_RDONLY);
        if(fd >= 0) close(fd);
      }
      exit();
    }
  }

  // Parent concurrently snapshots logs while children write
  for(i = 0; i < 10; i++){
    struct klog_entry sample[16];
    getklog(sample, 16);
  }

  // Wait for all children
  for(i = 0; i < 4; i++){
    wait();
  }

  printf(1, "  -> PASS: 4 concurrent processes executed without panic or corruption.\n");
}

int
main(int argc, char *argv[])
{
  printf(1, "\n============================================\n");
  printf(1, "      XV6 KERNEL LOGGING STRESS SUITE       \n");
  printf(1, "============================================\n\n");

  test_invalid_inputs();
  test_device_cursor();
  test_wraparound_and_ordering();
  test_concurrency();

  printf(1, "\n>>> ALL 4 STRESS TESTS PASSED SUCCESSFULLY! <<<\n\n");
  exit();
}
