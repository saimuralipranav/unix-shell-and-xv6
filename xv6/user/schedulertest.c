#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define N_PROCS 5

void cpu_bound_workload(int iters) {
  volatile unsigned long long x = 0;
  for (int i = 0; i < iters; i++) {
    for (int j = 0; j < 1000; j++) {
      x += (i * j);
    }
  }
}

void io_bound_workload(int count, int sleep_ticks) {
  volatile unsigned long long x = 0;
  for (int i = 0; i < count; i++) {
    for (int j = 0; j < 50000; j++) {
      x += j;
    }
    pause(sleep_ticks);
  }
}

void mixed_workload(int count, int iters, int sleep_ticks) {
  volatile unsigned long long x = 0;
  for (int i = 0; i < count; i++) {
    for (int j = 0; j < iters; j++) {
      for (int k = 0; k < 1000; k++) {
        x += (j * k);
      }
    }
    pause(sleep_ticks);
  }
}

int main(int argc, char *argv[]) {
  int pids[N_PROCS];
  char *types[N_PROCS] = {
    "CPU-Bound 1",
    "CPU-Bound 2",
    "I/O-Bound 1",
    "I/O-Bound 2",
    "Mixed       "
  };

  printf("=== Starting schedulertest (%d processes) ===\n", N_PROCS);

  for (int i = 0; i < N_PROCS; i++) {
    int pid = fork();
    if (pid < 0) {
      printf("fork failed\n");
      exit(1);
    }
    if (pid == 0) {
      if (i == 0) {
        cpu_bound_workload(3000000);
      } else if (i == 1) {
        cpu_bound_workload(3000000);
      } else if (i == 2) {
        io_bound_workload(20, 3);
      } else if (i == 3) {
        io_bound_workload(18, 4);
      } else {
        mixed_workload(10, 80000, 3);
      }
      exit(0);
    } else {
      pids[i] = pid;
    }
  }

  // Parent reaps all children and records metrics
  int wtimes[N_PROCS], rtimes[N_PROCS], ttimes[N_PROCS], rptimes[N_PROCS];
  int child_pids[N_PROCS];
  int total_wtime = 0, total_rtime = 0, total_ttime = 0, total_rptime = 0;

  for (int i = 0; i < N_PROCS; i++) {
    int w = 0, r = 0, t = 0, rp = 0;
    int reaped_pid = wait_all(&w, &r, &t, &rp);
    child_pids[i] = reaped_pid;
    wtimes[i] = w;
    rtimes[i] = r;
    ttimes[i] = t;
    rptimes[i] = rp;

    total_wtime += w;
    total_rtime += r;
    total_ttime += t;
    total_rptime += rp;
  }

  printf("\n======================= Process Completion Metrics =======================\n");
  printf("PID\tType\t\tTurnaround\tWaiting\t\tResponse\tRunTime\n");
  printf("--------------------------------------------------------------------------\n");
  for (int i = 0; i < N_PROCS; i++) {
    char *tname = "Unknown     ";
    for (int j = 0; j < N_PROCS; j++) {
      if (child_pids[i] == pids[j]) {
        tname = types[j];
        break;
      }
    }
    printf("%d\t%s\t%d ticks\t\t%d ticks\t\t%d ticks\t\t%d ticks\n",
           child_pids[i], tname, ttimes[i], wtimes[i], rptimes[i], rtimes[i]);
  }
  printf("--------------------------------------------------------------------------\n");
  printf("Average Turnaround Time: %d ticks\n", total_ttime / N_PROCS);
  printf("Average Waiting Time:    %d ticks\n", total_wtime / N_PROCS);
  printf("Average Response Time:   %d ticks\n", total_rptime / N_PROCS);
  printf("Average Run Time:        %d ticks\n", total_rtime / N_PROCS);
  printf("==========================================================================\n");

  exit(0);
}
