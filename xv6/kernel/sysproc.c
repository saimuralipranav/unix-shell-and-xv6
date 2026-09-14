#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

// Terminates the current process with the given status.
uint64
sys_exit(void)
{
  int n;
  argint(0, &n); // Extract exit code
  kexit(n);      // Terminate process
  return 0;      // Unreachable
}

// Returns the process ID of the calling process.
uint64
sys_getpid(void)
{
  return myproc()->pid; // Return current process PID
}

// Creates a new process by duplicating the calling process.
uint64
sys_fork(void)
{
  return kfork(); // Create child process
}

// Waits for a child process to exit and returns its PID.
uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p); // User address for exit status
  return kwait(p);
}

// waitx: returns wait time and run time of an exited child process
uint64
sys_waitx(void)
{
  uint64 wtime, rtime;
  argaddr(0, &wtime); // User buffer for wait ticks
  argaddr(1, &rtime); // User buffer for run ticks
  return kwaitx(wtime, rtime, 0, 0); // Delegate to kwaitx
}

// wait_all: returns wait, run, turnaround, and response times for benchmarking
uint64
sys_wait_all(void)
{
  uint64 wtime, rtime, ttime, rptime;
  argaddr(0, &wtime);  // Waiting ticks address
  argaddr(1, &rtime);  // Running ticks address
  argaddr(2, &ttime);  // Turnaround ticks address
  argaddr(3, &rptime); // Response ticks address
  return kwaitx(wtime, rtime, ttime, rptime); // Extended metric collection
}

// Increases or decreases the size of the process's data space.
// Supports both eager allocation and lazy allocation based on the 't' flag.
uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n); // Bytes to grow/shrink
  argint(1, &t); // Allocation policy
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) { // Eager allocation or shrinkage
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr) // Overflow check
      return -1;
    if (addr + n > TRAPFRAME) // Limit check
      return -1;
    myproc()->sz += n; // Expand size lazily
  }
  return addr;
}

// pause: puts calling process to sleep for n timer ticks
uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n); // Extract tick count
  if (n < 0)
    n = 0; // Clamp negative delay
  acquire(&tickslock);
  ticks0 = ticks; // Record initial ticks
  while (ticks - ticks0 < n) { // Sleep until n ticks elapse
    if (killed(myproc())) { // Abort if process killed
      release(&tickslock);
      return -1;
    }
    sleep_prepare(&ticks); // Prepare channel
    release(&tickslock);
    sleep();               // Deschedule
    acquire(&tickslock);
  }
  release(&tickslock);
  return 0; // Success
}

// Sends a kill signal to the process with the specified PID.
uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid); // Target PID
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock); // Acquire lock
  xticks = ticks;      // Read current ticks
  release(&tickslock);
  return xticks;
}
