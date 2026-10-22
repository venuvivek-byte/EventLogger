# High-Performance Resilient Kernel Logging Subsystem for xv6

An enterprise-grade, multi-core in-memory kernel logging subsystem for the xv6 operating system (x86), inspired by Linux's `dmesg` / kernel ring buffer and designed for high concurrency, memory safety, and post-mortem observability.

---

## 🚀 Key Highlights & Engineering Contributions

### 1. Root-Cause Bug Fixes & Kernel Memory Safety
* **Eliminated Kernel Stack Overflow (Issue 1 - Critical):**
  * *Bug:* `/dev/klog` previously allocated `struct klog_entry entries[64]` directly on the kernel stack (~5,632 bytes), immediately exceeding xv6's strict 4,096-byte per-process kernel stack and risking page-directory corruption.
  * *Fix:* Redesigned device read routine to use bounded single-entry streaming buffers (`sizeof(struct klog_entry) == 88 B`), completely preventing stack exhaustion.
* **Stream-Based Cursor Tracking (Issue 2 - High):**
  * *Bug:* `/dev/klog` called snapshot operations without advancing reader state, repeatedly returning identical log entries.
  * *Fix:* Implemented sequential cursor-based streaming (`klog_read_next`) using monotonic sequence matching to guarantee non-repeating event streams across consecutive reads.
* **Gap-Tolerant Chronological Merging (Issue 3 - High):**
  * *Bug:* The original snapshot logic assumed contiguous sequence numbers across per-CPU buffers (`min_seq + skip_count`). Because independent per-CPU buffers wrap around at different rates, sequence gaps caused valid records to be silently omitted.
  * *Fix:* Designed a k-way reverse priority merge that extracts the newest entries directly from per-CPU tail pointers based on true sequence numbers, preserving strict global ordering even under arbitrary wraparound patterns.
* **Lock-Free Atomic Sequencing (Issue 4 - Concurrency Optimization):**
  * *Bottleneck:* The original implementation claimed lock-free behavior, but bottlenecked all CPUs behind a global sequence spinlock (`seq_lock`).
  * *Fix:* Replaced the shared lock with hardware-level atomic fetch-and-add (`__sync_fetch_and_add`), achieving true wait-free sequence assignment without multi-core contention.
* **Defensive Formatter & System Call Validation:**
  * Fixed an off-by-one buffer overflow in format string processing.
  * Corrected missing overwrite metrics (`dropped` counter).
  * Hardened `sys_getklog` with overflow-safe user address space bounds checks and NULL pointer rejection.
  * Removed the silent 46-entry page clamp, enabling unconstrained snapshot retrieval into user buffers.

---

## 📊 Measured Benchmark Results (x86 QEMU)

Benchmarked using the newly authored `klog_bench` microbenchmark measuring cycle-accurate overhead via x86 `RDTSC`:

### 1. System Call Logging Overhead
| Metric | Cycles / Operation |
| :--- | :--- |
| **Minimum Latency** | **408,462 cycles** |
| **Average Latency** | **480,822 cycles** |
| **Maximum Latency** | **1,599,773 cycles** |

### 2. Snapshot Retrieval Latency (`getklog`)
| Operation | Metric |
| :--- | :--- |
| **64-entry merged snapshot** | 1,935,471 cycles |
| **Effective per-entry extraction & sorting** | **~30,241 cycles / entry** |

### 3. Multi-Core Concurrency Scaling
Measuring effective cycles per event across concurrent worker processes:
| Concurrent Workers | Total Events | Total Elapsed Cycles | Cycles / Event | Scalability Overhead |
| :---: | :---: | :---: | :---: | :---: |
| **1 Worker** | 200 | 101,584,720 | **507,923 cycles** | Baseline |
| **2 Workers** | 400 | 208,690,479 | **521,726 cycles** | **+2.7%** |
| **4 Workers** | 800 | 426,145,540 | **532,681 cycles** | **+4.8%** |

*Analysis:* Thanks to atomic sequence allocation and per-CPU partitioned ring buffers, increasing from 1 to 4 concurrent processes adds less than 5% synchronization overhead, preventing cross-core lock starvation.

---

## 🧪 Automated Test & Stress Suite

Run the automated test suite directly within xv6:

```bash
$ klog_test
=== Kernel Logging System Test ===
Testing getklog() syscall...
Retrieved 9 log entries:
  [0] CPU0 PID0: klog: logging subsystem initialized
  ...
Testing /dev/klog device...
Read 5 entries from device with verified cursor advancement
=== Test Complete ===
```

Run the stress suite verifying edge cases and concurrency:
```bash
$ klog_stress
============================================
      XV6 KERNEL LOGGING STRESS SUITE       
============================================
[TEST 1] System call parameter validation & safety...
  -> PASS: All invalid input edge-cases rejected cleanly.
[TEST 2] /dev/klog cursor advancement & stream consistency...
  -> PASS: Read 10 entries; cursor strictly advanced without repeats.
[TEST 3] Ring buffer wraparound and snapshot ordering...
  -> PASS: Wraparound handled correctly; 64 entries chronologically ordered.
[TEST 4] Concurrent multi-process stress test...
  -> PASS: 4 concurrent processes executed without panic or corruption.

>>> ALL 4 STRESS TESTS PASSED SUCCESSFULLY! <<<
```

---

## 🛠️ How to Build and Run

### Requirements
- GCC (supporting 32-bit compilation, `-m32`)
- QEMU (`qemu-system-i386`)
- GNU Make & Perl

### Commands
```bash
cd xv6-public

# Compile kernel and generate filesystem
make clean
make
make fs.img

# Launch in QEMU (terminal mode)
make qemu-nox

# Run tests inside xv6 shell:
$ klog_test
$ klog_stress
$ klog_bench
$ ulog_tool
```
*(To exit QEMU: press `Ctrl-A` followed by `X`)*

---

## 📄 Resume Bullet Points (Tailored for Systems / AMD Roles)

```text
High-Performance Resilient Kernel Logging Subsystem | C, x86 Assembly, xv6
• Engineered a multi-core kernel logging subsystem with per-CPU circular buffers and lock-free sequence allocation using atomic x86 primitives (__sync_fetch_and_add), achieving <5% scaling overhead from 1 to 4 cores.
• Resolved critical kernel memory safety vulnerability by replacing unbounded stack allocations with bounded streaming buffers, eliminating stack-overflow risk within xv6's 4KB per-process kernel stack.
• Architected a k-way reverse priority merge algorithm ensuring strict monotonic event ordering across multi-core buffer wraparounds and sequence gaps.
• Implemented a UNIX-compliant character device driver (/dev/klog) with stateful cursor tracking and developed an automated RDTSC-based microbenchmark suite measuring cycle-level syscall latency.
```
