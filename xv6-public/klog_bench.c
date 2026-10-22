// Kernel Logging Subsystem Microbenchmark Suite
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

// Read low 32 bits of x86 Time-Stamp Counter (RDTSC)
// Unsigned 32-bit arithmetic accurately computes differences for intervals < 2^32 cycles.
static inline uint
rdtsc_lo(void)
{
  uint lo;
  asm volatile("rdtsc" : "=a"(lo) : : "edx");
  return lo;
}

// Benchmark 1: System call logging overhead
void
bench_syscall_latency(int iterations)
{
  int i, fd;
  uint start, end, total = 0;
  uint min_cycles = 0xFFFFFFFF;
  uint max_cycles = 0;

  printf(1, "[BENCH 1] Measuring logged syscall latency (%d iterations)...\n", iterations);

  for(i = 0; i < iterations; i++){
    start = rdtsc_lo();
    fd = open(".", O_RDONLY);
    if(fd >= 0) close(fd);
    end = rdtsc_lo();

    uint diff = end - start;
    total += diff;
    if(diff < min_cycles) min_cycles = diff;
    if(diff > max_cycles) max_cycles = diff;
  }

  uint avg = total / iterations;
  printf(1, "  -> Average Latency : %d cycles/op\n", avg);
  printf(1, "  -> Min Latency     : %d cycles/op\n", min_cycles);
  printf(1, "  -> Max Latency     : %d cycles/op\n\n", max_cycles);
}

// Benchmark 2: Snapshot retrieval throughput
void
bench_snapshot_latency(int iterations)
{
  int i;
  struct klog_entry *entries = (struct klog_entry*)malloc(64 * sizeof(struct klog_entry));
  uint start, end, total = 0;

  if(entries == 0){
    printf(1, "malloc failed\n");
    return;
  }

  printf(1, "[BENCH 2] Measuring getklog(64) snapshot latency (%d iterations)...\n", iterations);

  for(i = 0; i < iterations; i++){
    start = rdtsc_lo();
    getklog(entries, 64);
    end = rdtsc_lo();
    total += (end - start);
  }

  uint avg = total / iterations;
  printf(1, "  -> Average Snapshot Time : %d cycles (for 64 merged entries)\n", avg);
  printf(1, "  -> Per-entry extraction  : %d cycles/entry\n\n", avg / 64);
  free(entries);
}

// Benchmark 3: Multi-worker concurrency scaling
void
bench_concurrency_scaling(int workers, int iters_per_worker)
{
  int pids[8];
  int i, j;
  uint start, end;

  printf(1, "[BENCH 3] Concurrency throughput with %d concurrent worker(s)...\n", workers);

  start = rdtsc_lo();
  for(i = 0; i < workers; i++){
    pids[i] = fork();
    if(pids[i] < 0){
      printf(1, "fork error\n");
      exit();
    }
    if(pids[i] == 0){
      for(j = 0; j < iters_per_worker; j++){
        int fd = open(".", O_RDONLY);
        if(fd >= 0) close(fd);
      }
      exit();
    }
  }

  for(i = 0; i < workers; i++){
    wait();
  }
  end = rdtsc_lo();

  uint total_cycles = end - start;
  int total_events = workers * iters_per_worker;
  uint cycles_per_event = total_cycles / total_events;

  printf(1, "  -> Total Events Processed : %d\n", total_events);
  printf(1, "  -> Total Elapsed Cycles   : %d\n", total_cycles);
  printf(1, "  -> Effective Cost/Event   : %d cycles\n\n", cycles_per_event);
}

int
main(int argc, char *argv[])
{
  printf(1, "====================================================\n");
  printf(1, "     AMD-FOCUSED KERNEL LOGGING BENCHMARK SUITE     \n");
  printf(1, "====================================================\n\n");

  bench_syscall_latency(500);
  bench_snapshot_latency(200);

  printf(1, "--- Scaling Analysis Across Workers ---\n");
  bench_concurrency_scaling(1, 200);
  bench_concurrency_scaling(2, 200);
  bench_concurrency_scaling(4, 200);

  printf(1, "====================================================\n");
  printf(1, "BENCHMARK COMPLETE. Collect cycle numbers for report.\n");
  printf(1, "====================================================\n\n");

  exit();
}
