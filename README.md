# Mini-Project 1: C-Shell & xv6 MLFQ Scheduler

Operating Systems and Networks (OSN) — Mini-Project 1.

---

## 1. Repository Structure

```
mini-project1/
├── c-shell/
│   ├── src/          # Implementation (.c) files
│   ├── include/      # Header (.h) files
│   └── Makefile      # POSIX-compliant build rules
├── xv6/
│   ├── kernel/       # xv6 OS kernel (MLFQ, FIFO, RR scheduler implementations)
│   ├── user/         # User programs (including custom schedulertest)
│   ├── mkfs/         # Filesystem builder
│   ├── Makefile      # xv6 build system with SCHEDULER macro
│   └── report.md     # Performance comparison & MLFQ timeline analysis
├── AI-usage.txt      # Documented AI usage & prompts
└── README.md         # Project documentation
```

---

## 2. C-Shell

A custom POSIX-compliant interactive Unix shell implementing:
* **Part A: Shell Input**: Custom lexer and right-linear grammar parser validating quotes, escapes, operators, and syntax with informative prompts.
* **Part B: Intrinsics**: `hop` (with persistent frecency decay scoring), `reveal` (with `-a` and recursive `-t`), `peek` (forward/reverse line viewer using `lseek`), and `locate` (PATH and CWD executable resolution).
* **Part C: Redirection & Pipes**: Arbitrary pipelines (`|`), input redirection (`<`) with multiple file concatenation, duplicate output redirection (`>` and `>>`), and builtin pipeline execution.
* **Part D: Sequential & Background Execution**: Semicolon (`;`) sequential execution and ampersand (`&`) background jobs with session-wide monotonically increasing job numbers, `SIGCHLD` handling, and deferred exit notifications.
* **Part E: Exotic Intrinsics & Terminal Control**:
  * `activities`: Running/stopped process listing grouped by process group and ordered by launch time.
  * Terminal control: Proper process grouping (`setpgid`), foreground terminal switching (`tcsetpgrp`), `Ctrl-C`, `Ctrl-Z`, and `Ctrl-D` handling with `SIGHUP` propagation on shell exit.
  * `resume`: Resumes stopped/background jobs to foreground (`fg [--timeout <sec>]`) or background (`bg`).
  * `ping`: Delivers signals modulo 64 to specific PIDs or process groups (`%job_number`).
* **Part F: Diagnostics**:
  * `spy`: Inspects open file descriptors, memory maps, text references, and working directories from `/proc`.
  * `snoop`: Traces system call counts and execution times using `ptrace` for new or existing processes.

### Build & Run
```bash
cd c-shell/
make all        # Produces shell.out
./shell.out     # Launches shell
make clean      # Removes build artifacts
```

### Key Specifications & Assumptions
* **Max Input Length**: 1024 characters (`MAX_INPUT`).
* **Max Path Length**: 4096 characters (`MAX_PATH`).
* **Max Tracked Jobs**: 1024 concurrent process groups (`MAX_TRACKED_JOBS`).
* **Pipelines**: Dynamically allocated structures supporting arbitrary pipeline lengths.

---

## 3. xv6 MLFQ Scheduler

An implementation of a Multi-Level Feedback Queue (MLFQ) CPU scheduling policy integrated into the RISC-V xv6 kernel alongside Round Robin (RR) and First-In First-Out (FIFO).

### Features
* **4 Priority Queues**: $Q_0$ (1 tick), $Q_1$ (4 ticks), $Q_2$ (8 ticks), $Q_3$ (16 ticks).
* **Scheduling Rules**:
  * New processes enter at the tail of $Q_0$.
  * Highest-priority runnable queue is always selected.
  * Preemption at tick boundaries when higher priority processes arrive.
  * Demotion to lower priority queue upon full quantum exhaustion.
  * Voluntary yields (I/O, `sleep`) preserve queue level.
  * Priority boosting every 48 timer ticks resets all active processes to $Q_0$ to eliminate starvation.
* **Debugging**: Extended `Ctrl+P` (`procdump`) prints PID, state, queue level, quantum usage, run time, and wait time.
* **Benchmarking**: Custom user-space test harness (`schedulertest`) evaluating turnaround time, waiting time, and response time across CPU-bound, I/O-bound, and mixed workloads.

### Build & Run
```bash
cd xv6/

# Default Round Robin (RR)
make clean && make qemu

# Multi-Level Feedback Queue (MLFQ)
make clean && make qemu SCHEDULER=MLFQ

# FIFO Scheduler
make clean && make qemu SCHEDULER=FIFO
```

To run the scheduler benchmarks inside xv6:
```
$ schedulertest
```

To generate the scheduling timeline plot:
```bash
python3 generate_plot.py mlfq_output.txt mlfq_timeline.svg
```
The analysis report and comparison metrics are available in `xv6/report.md`.
