#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Total number of child benchmark processes
#define N_PROCS 5

// CPU-bound: heavy compute loops, uses full time slices
void cpu_bound_workload(int iters) {
  volatile unsigned long long x = 0; // Accumulator preventing compiler optimization
  for (int i = 0; i < iters; i++) {   // Outer computation loop
    for (int j = 0; j < 1000; j++) {  // Inner compute loop
      x += (i * j);                   // Arithmetic operations
    }
  }
}

// I/O-bound: brief compute followed by pause() sleep
void io_bound_workload(int count, int sleep_ticks) {
  volatile unsigned long long x = 0; // Accumulator
  for (int i = 0; i < count; i++) {  // Sleep-wake cycles
    for (int j = 0; j < 50000; j++) { // Short burst
      x += j;                        // Accumulate
    }
    pause(sleep_ticks);              // Yield and sleep
  }
}

// Mixed: moderate compute bursts with periodic sleeps
void mixed_workload(int count, int iters, int sleep_ticks) {
  volatile unsigned long long x = 0; // Accumulator
  for (int i = 0; i < count; i++) {  // Mixed loop cycles
    for (int j = 0; j < iters; j++) { // Moderate compute
      for (int k = 0; k < 1000; k++) { // Inner loop
        x += (j * k);                 // Operation
      }
    }
    pause(sleep_ticks);              // Periodic sleep
  }
}

// Entry point: runs benchmarks and prints timing statistics
int main(int argc, char *argv[]) {
  int pids[N_PROCS]; // Child PID array
  char *types[N_PROCS] = { // Descriptive labels
    "CPU-Bound 1",
    "CPU-Bound 2",
    "I/O-Bound 1",
    "I/O-Bound 2",
    "Mixed       "
  };

  printf("=== Starting schedulertest (%d processes) ===\n", N_PROCS);

  // Spawn 5 test processes with different workloads
  for (int i = 0; i < N_PROCS; i++) {
    int pid = fork(); // Fork benchmark child
    if (pid < 0) {    // Error handling
      printf("fork failed\n");
      exit(1);
    }
    if (pid == 0) { // Child branch
      if (i == 0) {
        cpu_bound_workload(3000000); // Heavy CPU 1
      } else if (i == 1) {
        cpu_bound_workload(3000000); // Heavy CPU 2
      } else if (i == 2) {
        io_bound_workload(20, 3);    // I/O bound 1
      } else if (i == 3) {
        io_bound_workload(18, 4);    // I/O bound 2
      } else {
        mixed_workload(10, 80000, 3); // Mixed workload
      }
      exit(0); // Exit child cleanly
    } else {
      pids[i] = pid; // Save child PID in parent
    }
  }

  // Parent reaps all children and collects metrics via wait_all()
  int wtimes[N_PROCS], rtimes[N_PROCS], ttimes[N_PROCS], rptimes[N_PROCS]; // Metrics arrays
  int child_pids[N_PROCS]; // Exit order PIDs
  int total_wtime = 0, total_rtime = 0, total_ttime = 0, total_rptime = 0; // Totals for averages

  // Harvest exit stats
  for (int i = 0; i < N_PROCS; i++) {
    int w = 0, r = 0, t = 0, rp = 0;
    int reaped_pid = wait_all(&w, &r, &t, &rp); // Collect process metrics
    child_pids[i] = reaped_pid;
    wtimes[i] = w;   // Wait time
    rtimes[i] = r;   // Run time
    ttimes[i] = t;   // Turnaround time
    rptimes[i] = rp; // Response time

    // Accumulate sum totals
    total_wtime += w;
    total_rtime += r;
    total_ttime += t;
    total_rptime += rp;
  }

  // Print results table
  printf("\n======================= Process Completion Metrics =======================\n");
  printf("PID\tType\t\tTurnaround\tWaiting\t\tResponse\tRunTime\n");
  printf("--------------------------------------------------------------------------\n");
  for (int i = 0; i < N_PROCS; i++) {
    char *tname = "Unknown     "; // Fallback label
    for (int j = 0; j < N_PROCS; j++) {
      if (child_pids[i] == pids[j]) { // Match PID to original type
        tname = types[j];
        break;
      }
    }
    printf("%d\t%s\t%d ticks\t\t%d ticks\t\t%d ticks\t\t%d ticks\n",
           child_pids[i], tname, ttimes[i], wtimes[i], rptimes[i], rtimes[i]);
  }
  printf("--------------------------------------------------------------------------\n");
  // Print average summary
  printf("Average Turnaround Time: %d ticks\n", total_ttime / N_PROCS);
  printf("Average Waiting Time:    %d ticks\n", total_wtime / N_PROCS);
  printf("Average Response Time:   %d ticks\n", total_rptime / N_PROCS);
  printf("Average Run Time:        %d ticks\n", total_rtime / N_PROCS);
  printf("==========================================================================\n");

  exit(0); // Success
}
