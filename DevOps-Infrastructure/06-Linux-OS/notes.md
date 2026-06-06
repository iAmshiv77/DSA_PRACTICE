# Linux & Operating Systems — Interview Deep Dive

## OS Concepts (Interview Essentials)

### Process vs Thread vs Coroutine

```
Process:
  - Independent memory space (virtual address space)
  - Heavy context switch (save all registers, TLB flush)
  - IPC: pipes, sockets, shared memory, signals
  - Isolation: one process crash doesn't affect another

Thread:
  - Shared memory space within a process
  - Lightweight context switch (save thread registers only)
  - Communication: shared memory (need locks)
  - Risk: one thread crash can kill the whole process

Coroutine:
  - User-space cooperative scheduling (no kernel involvement)
  - No context switch overhead (just function pointer + stack swap)
  - Must explicitly yield (cooperative, not preemptive)
  - Used in: Node.js async/await, Go goroutines, Python asyncio

Context Switch Cost:
  Thread→Thread (same process): ~1–10 µs (save/restore registers, L1/L2 cache flush)
  Process→Process: ~5–100 µs (TLB flush, memory mapping)
  Goroutine→Goroutine: ~100 ns (user-space, minimal)
```

### Memory Layout of a Process

```
High addresses ┌──────────────────┐
               │     Stack        │ ← grows down
               │   (local vars,   │
               │    return addrs) │
               ├──────────────────┤
               │      ↓ grows     │
               │                  │
               │      ↑ grows     │
               ├──────────────────┤
               │      Heap        │ ← dynamic alloc (malloc/new)
               ├──────────────────┤
               │  BSS Segment     │ ← uninitialized globals (zero-filled)
               ├──────────────────┤
               │  Data Segment    │ ← initialized globals/statics
               ├──────────────────┤
               │  Code (Text)     │ ← executable instructions (read-only)
Low addresses  └──────────────────┘

Stack overflow: stack grows into heap → segfault
Heap fragmentation: many small allocs/frees → holes in heap
```

### Virtual Memory & Paging

```
Virtual Address → Page Table → Physical Address

Page size: 4 KB (typical)
Each process has its own virtual address space (32-bit: 4 GB, 64-bit: 128 TB)

Page fault:
  - Process accesses virtual page not in physical memory
  - OS interrupts, loads page from disk (swap) → slow
  - If no swap → OOM killer

TLB (Translation Lookaside Buffer):
  - Cache for page table entries
  - Hit rate ~99% (otherwise huge overhead)
  - TLB flush on context switch (process change) → why process switch is expensive

Demand paging: pages loaded only when accessed (not all at once)
Copy-on-write: fork() shares pages until write → then copies
```

### CPU Scheduling

```
Round Robin: each process gets time quantum (10–100ms). Fair but overhead.
Priority Scheduling: higher priority first. Risk: starvation.
Shortest Job First (SJF): minimize avg wait time. Needs future knowledge.
Completely Fair Scheduler (CFS): Linux default.
  - Virtual runtime: tracks CPU time × weight
  - Always picks task with lowest virtual runtime
  - Red-black tree for O(log N) task selection

Real-time scheduling: SCHED_FIFO, SCHED_RR (for latency-critical processes)
```

### Deadlock

```
4 Conditions (Coffman): ALL must hold for deadlock
  1. Mutual Exclusion: resources can't be shared
  2. Hold and Wait: process holds resource while waiting for another
  3. No Preemption: resources can't be forcibly taken
  4. Circular Wait: A waits for B, B waits for C, C waits for A

Detection: Resource Allocation Graph. Cycle = potential deadlock.

Prevention: Break one condition:
  - Order all locks globally (no circular wait)
  - Try-lock with timeout (breaks hold-and-wait)
  - Release all before acquiring more

Banker's Algorithm: check if resource allocation leads to safe state
  (Used in textbooks, not practical for OS due to complexity)
```

### Synchronization Primitives

```
Mutex: Binary lock. One thread at a time. Ownership (locker must unlock).
Semaphore: Counter. Multiple threads up to count. No ownership.
  - Binary semaphore ≈ Mutex but no ownership
  - Counting semaphore: N threads can hold (e.g., connection pool)

Spinlock: Busy-wait (burns CPU). Use only for very short critical sections.
Read-Write Lock: Multiple readers OR one writer. readers > 1 OK.
Condition Variable: Wait for condition + mutex. notify()/wait() pattern.
Atomic: Lock-free. CPU guarantees single instruction. (test-and-set, CAS)

CAS (Compare-And-Swap):
  if (memory == expected) { memory = new_value; return true; }
  Used to implement lock-free data structures.
```

---

## Linux Commands — Interview Essentials

### Process Management
```bash
ps aux                          # list all processes
ps aux | grep nginx             # find specific process
top / htop                      # real-time process monitor
kill -9 PID                     # force kill
kill -15 PID                    # graceful termination (SIGTERM)
nice -n 10 command              # run with lower priority
renice -n -5 -p PID             # change priority of running process
nohup command &                 # run in background, immune to hangup
disown %1                       # detach job from shell
strace -p PID                   # trace system calls (debugging)
lsof -p PID                     # list open files by process
```

### File System
```bash
ls -la                          # long listing with hidden files
find / -name "*.log" -mtime +7  # files older than 7 days
find / -size +100M              # files larger than 100MB
du -sh /var/log                 # disk usage of directory
df -h                           # disk usage of mounted filesystems
chmod 755 file                  # owner:rwx, group:r-x, others:r-x
chown user:group file           # change ownership
ln -s /target /link             # symbolic link
inode: stores metadata, not filename. Filename → inode → data blocks
```

### Networking
```bash
netstat -tuln                   # listening ports
ss -tuln                        # faster alternative to netstat
curl -I https://example.com     # HTTP headers
wget -O file.txt URL            # download file
ping 8.8.8.8                    # test connectivity
traceroute google.com           # show routing path
nslookup / dig example.com      # DNS lookup
nc -zv host 80                  # test port connectivity
iptables -L                     # list firewall rules
tcpdump -i eth0 port 80         # capture network packets
```

### Performance Investigation
```bash
# CPU
vmstat 1                        # CPU, memory, I/O every 1 sec
mpstat -P ALL 1                 # per-CPU stats
top → press '1'                 # per-core CPU usage

# Memory
free -h                         # memory usage
cat /proc/meminfo               # detailed memory info
vmstat -s                       # memory statistics

# Disk I/O
iostat -x 1                     # extended disk I/O stats
iotop                           # per-process I/O (like top)
dstat -d                        # disk read/write in real-time

# Network
iftop                           # per-connection bandwidth
nethogs                         # per-process bandwidth
sar -n DEV 1                    # historical network stats
```

### System Configuration
```bash
ulimit -n                       # max open file descriptors
ulimit -n 65536                 # set limit (session)
# Permanent: /etc/security/limits.conf
# sysctl settings (kernel params):
sysctl net.core.somaxconn       # max listen backlog
sysctl vm.swappiness=10         # reduce swap usage
echo 1 > /proc/sys/net/ipv4/ip_forward  # enable IP forwarding
```

---

## File Descriptors & I/O Models

```
Everything in Linux is a file:
  Regular files, directories, sockets, pipes, devices

File Descriptor (FD): integer handle to an open file
  0 = stdin, 1 = stdout, 2 = stderr
  App opens file → FD returned → read/write using FD

I/O Models:
┌────────────────┬────────────────────────────────────────────┐
│ Blocking I/O   │ Thread waits until data ready. Simple.     │
│ Non-blocking   │ Returns immediately. App polls. CPU waste. │
│ I/O Multiplexing│ select()/poll()/epoll: monitor many FDs.  │
│ Async I/O (AIO)│ Kernel notifies when complete. Complex.    │
└────────────────┴────────────────────────────────────────────┘

epoll (Linux):
  - Efficient for 10K+ connections (O(1) vs O(N) for select)
  - Edge-triggered: notify only on change
  - Level-triggered: notify while data available
  - Used by: Node.js libuv, Nginx, Redis

Why Node.js is fast despite single thread:
  → Uses epoll via libuv event loop
  → Non-blocking I/O: thread never waits
  → While waiting for I/O, serves other requests
  → Bottleneck is only CPU-bound work
```

---

## Signals

```
Common signals:
  SIGTERM (15): Graceful shutdown request. Process can handle.
  SIGKILL  (9): Forced kill. Cannot be caught or ignored.
  SIGINT   (2): Ctrl+C. Usually causes graceful shutdown.
  SIGHUP   (1): Hang-up. Often used to reload config.
  SIGSEGV (11): Segmentation fault. Invalid memory access.
  SIGCHLD (17): Child process terminated/stopped.

Handling in code:
  signal(SIGTERM, handler_function);  // register handler
  On SIGTERM: close DB connections, finish in-flight requests, exit
```

---

## OS Interview Questions

**Q: What happens when you type `ls` in the terminal?**
A: (1) Shell looks up `ls` in PATH, finds `/bin/ls`. (2) `fork()` creates child process. (3) `exec()` replaces child with `ls` binary. (4) `ls` opens directory via `opendir()` syscall. (5) Kernel looks up directory inode, reads entries. (6) `ls` writes to stdout (FD 1) via `write()` syscall. (7) Child exits, shell gets SIGCHLD, reaps child with `wait()`.

**Q: What is a zombie process?**
A: A process that has finished executing but still has an entry in the process table because its parent hasn't called `wait()`. The parent must reap the child to remove it. If parent dies first (orphan), init/systemd adopts and reaps it.

**Q: Explain what a system call is.**
A: Privileged operation that requires kernel mode (switch from user mode). Application calls wrapper (e.g., `open()`), which invokes `syscall` instruction → CPU switches to kernel mode → kernel executes → return to user mode. Costs ~100ns due to mode switch. That's why minimizing syscalls matters for performance.

**Q: How does virtual memory provide isolation between processes?**
A: Each process has its own page table. Virtual addresses are mapped to different physical addresses. Process A's virtual address 0x1000 maps to physical 0x5000. Process B's virtual address 0x1000 maps to physical 0x9000. No overlap possible — guaranteed by hardware MMU.

**Q: What is the difference between a mutex and a semaphore?**
A: Mutex: binary, has ownership (only locker can unlock), used for mutual exclusion. Semaphore: counter, no ownership, used for signaling/resource counting. A semaphore can be signaled by a different thread than the one that waited. Use mutex for critical sections; semaphore for producer-consumer signaling.

**Q: What causes a segmentation fault?**
A: Accessing memory that your process has no permission for: null pointer dereference, buffer overflow (writing past array bounds), accessing freed memory (dangling pointer), stack overflow. Kernel sends SIGSEGV to the process.

**Q: How does `fork()` work with copy-on-write?**
A: `fork()` creates an exact copy. Instead of copying all memory immediately, both processes share the same physical pages (marked read-only). On first write to a shared page, kernel copies that page (copy-on-write). Makes `fork()` + `exec()` fast — exec replaces memory, so most copied pages are never used.
