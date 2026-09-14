#include "snoop.h"
#include "exec.h"
#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>

// Linux x86_64 system call table mapping numbers to human names
static const char *syscall_names[] = {
    [0] = "read", [1] = "write", [2] = "open", [3] = "close",
    [4] = "stat", [5] = "fstat", [6] = "lstat", [7] = "poll",
    [8] = "lseek", [9] = "mmap", [10] = "mprotect", [11] = "munmap",
    [12] = "brk", [13] = "rt_sigaction", [14] = "rt_sigprocmask", [15] = "rt_sigreturn",
    [16] = "ioctl", [17] = "pread64", [18] = "pwrite64", [19] = "readv",
    [20] = "writev", [21] = "access", [22] = "pipe", [23] = "select",
    [24] = "sched_yield", [25] = "mremap", [26] = "msync", [27] = "mincore",
    [28] = "madvise", [29] = "shmget", [30] = "shmat", [31] = "shmctl",
    [32] = "dup", [33] = "dup2", [34] = "pause", [35] = "nanosleep",
    [36] = "getitimer", [37] = "alarm", [38] = "setitimer", [39] = "getpid",
    [40] = "sendfile", [41] = "socket", [42] = "connect", [43] = "accept",
    [44] = "sendto", [45] = "recvfrom", [46] = "sendmsg", [47] = "recvmsg",
    [48] = "shutdown", [49] = "bind", [50] = "listen", [51] = "getsockname",
    [52] = "getpeername", [53] = "socketpair", [54] = "setsockopt", [55] = "getsockopt",
    [56] = "clone", [57] = "fork", [58] = "vfork", [59] = "execve",
    [60] = "exit", [61] = "wait4", [62] = "kill", [63] = "uname",
    [64] = "semget", [65] = "semop", [66] = "semctl", [67] = "shmdt",
    [68] = "msgget", [69] = "msgsnd", [70] = "msgrcv", [71] = "msgctl",
    [72] = "fcntl", [73] = "flock", [74] = "fsync", [75] = "fdatasync",
    [76] = "truncate", [77] = "ftruncate", [78] = "getdents", [79] = "getcwd",
    [80] = "chdir", [81] = "fchdir", [82] = "rename", [83] = "mkdir",
    [84] = "rmdir", [85] = "creat", [86] = "link", [87] = "unlink",
    [88] = "symlink", [89] = "readlink", [90] = "chmod", [91] = "fchmod",
    [92] = "chown", [93] = "fchown", [94] = "lchown", [95] = "umask",
    [96] = "gettimeofday", [97] = "getrlimit", [98] = "getrusage", [99] = "sysinfo",
    [100] = "times", [101] = "ptrace", [102] = "getuid", [103] = "syslog",
    [104] = "getgid", [105] = "setuid", [106] = "setgid", [107] = "geteuid",
    [108] = "getegid", [109] = "setpgid", [110] = "getppid", [111] = "getpgrp",
    [112] = "setsid", [113] = "setreuid", [114] = "setregid", [115] = "getgroups",
    [116] = "setgroups", [117] = "setresuid", [118] = "getresuid", [119] = "setresgid",
    [120] = "getresgid", [121] = "getpgid", [122] = "setfsuid", [123] = "setfsgid",
    [124] = "getsid", [125] = "capget", [126] = "capset", [127] = "rt_sigpending",
    [128] = "rt_sigtimedwait", [129] = "rt_sigqueueinfo", [130] = "rt_sigsuspend",
    [131] = "sigaltstack", [132] = "utime", [133] = "mknod", [134] = "uselib",
    [135] = "personality", [136] = "ustat", [137] = "statfs", [138] = "fstatfs",
    [139] = "sysfs", [140] = "getpriority", [141] = "setpriority", [142] = "sched_setparam",
    [143] = "sched_getparam", [144] = "sched_setscheduler", [145] = "sched_getscheduler",
    [146] = "sched_get_priority_max", [147] = "sched_get_priority_min", [148] = "sched_rr_get_interval",
    [149] = "mlock", [150] = "munlock", [151] = "mlockall", [152] = "munlockall",
    [153] = "vhangup", [154] = "modify_ldt", [155] = "pivot_root", [156] = "_sysctl",
    [157] = "prctl", [158] = "arch_prctl", [159] = "adjtimex", [160] = "setrlimit",
    [161] = "chroot", [162] = "sync", [163] = "acct", [164] = "settimeofday",
    [165] = "mount", [166] = "umount2", [167] = "swapon", [168] = "swapoff",
    [169] = "reboot", [170] = "sethostname", [171] = "setdomainname", [172] = "iopl",
    [173] = "ioperm", [174] = "create_module", [175] = "init_module", [176] = "delete_module",
    [177] = "get_kernel_syms", [178] = "query_module", [179] = "quotactl", [180] = "nfsservctl",
    [181] = "getpmsg", [182] = "putpmsg", [183] = "afs_syscall", [184] = "tuxcall",
    [185] = "security", [186] = "gettid", [187] = "readahead", [188] = "setxattr",
    [189] = "lsetxattr", [190] = "fsetxattr", [191] = "getxattr", [192] = "lgetxattr",
    [193] = "fgetxattr", [194] = "listxattr", [195] = "llistxattr", [196] = "flistxattr",
    [197] = "removexattr", [198] = "lremovexattr", [199] = "fremovexattr", [200] = "tkill",
    [201] = "time", [202] = "futex", [203] = "sched_setaffinity", [204] = "sched_getaffinity",
    [205] = "set_thread_area", [206] = "io_setup", [207] = "io_destroy", [208] = "io_getevents",
    [209] = "io_submit", [210] = "io_cancel", [211] = "get_thread_area", [212] = "lookup_dcookie",
    [213] = "epoll_create", [214] = "epoll_ctl_old", [215] = "epoll_wait_old", [216] = "remap_file_pages",
    [217] = "getdents64", [218] = "set_tid_address", [219] = "restart_syscall", [220] = "semtimedop",
    [221] = "fadvise64", [222] = "timer_create", [223] = "timer_settime", [224] = "timer_gettime",
    [225] = "timer_getoverrun", [226] = "timer_delete", [227] = "clock_settime", [228] = "clock_gettime",
    [229] = "clock_getres", [230] = "clock_nanosleep", [231] = "exit_group", [232] = "epoll_wait",
    [233] = "epoll_ctl", [234] = "tgkill", [235] = "utimes", [236] = "vserver",
    [237] = "mbind", [238] = "set_mempolicy", [239] = "get_mempolicy", [240] = "mq_open",
    [241] = "mq_unlink", [242] = "mq_timedsend", [243] = "mq_timedreceive", [244] = "mq_notify",
    [245] = "mq_getsetattr", [246] = "kexec_load", [247] = "waitid", [248] = "add_key",
    [249] = "request_key", [250] = "keyctl", [251] = "ioprio_set", [252] = "ioprio_get",
    [253] = "inotify_init", [254] = "inotify_add_watch", [255] = "inotify_rm_watch", [256] = "migrate_pages",
    [257] = "openat", [258] = "mkdirat", [259] = "mknodat", [260] = "fchownat",
    [261] = "futimesat", [262] = "newfstatat", [263] = "unlinkat", [264] = "renameat",
    [265] = "linkat", [266] = "symlinkat", [267] = "readlinkat", [268] = "fchmodat",
    [269] = "faccessat", [270] = "pselect6", [271] = "ppoll", [272] = "unshare",
    [273] = "set_robust_list", [274] = "get_robust_list", [275] = "splice", [276] = "tee",
    [277] = "sync_file_range", [278] = "vmsplice", [279] = "move_pages", [280] = "utimensat",
    [281] = "epoll_pwait", [282] = "signalfd", [283] = "timerfd_create", [284] = "eventfd",
    [285] = "fallocate", [286] = "timerfd_settime", [287] = "timerfd_gettime", [288] = "accept4",
    [289] = "signalfd4", [290] = "eventfd2", [291] = "epoll_create1", [292] = "dup3",
    [293] = "pipe2", [294] = "inotify_init1", [295] = "preadv", [296] = "pwritev",
    [297] = "rt_tgsigqueueinfo", [298] = "perf_event_open", [299] = "recvmmsg", [300] = "fanotify_init",
    [301] = "fanotify_mark", [302] = "prlimit64", [303] = "name_to_handle_at", [304] = "open_by_handle_at",
    [305] = "clock_adjtime", [306] = "syncfs", [307] = "sendmmsg", [308] = "setns",
    [309] = "getcpu", [310] = "process_vm_readv", [311] = "process_vm_writev", [312] = "kcmp",
    [313] = "finit_module", [314] = "sched_setattr", [315] = "sched_getattr", [316] = "renameat2",
    [317] = "seccomp", [318] = "getrandom", [319] = "memfd_create", [320] = "kexec_file_load",
    [321] = "bpf", [322] = "execveat", [323] = "userfaultfd", [324] = "membarrier",
    [325] = "mlock2", [326] = "copy_file_range", [327] = "preadv2", [328] = "pwritev2",
    [329] = "pkey_mprotect", [330] = "pkey_alloc", [331] = "pkey_free", [332] = "statx",
    [439] = "faccessat2", [440] = "process_madvise"
};

// Highest index represented in syscall_names table
#define SYSCALL_MAX ((int)(sizeof(syscall_names) / sizeof(syscall_names[0])))

// Data structure tracking per-syscall execution metrics
typedef struct {
    long sc_num;        // Syscall number
    char name[64];      // Syscall name
    int calls;          // Total number of invocations
    double total_time;  // Cumulative elapsed execution time
    int first_order;    // Tie-breaker order of appearance
} SnoopStat;

#define MAX_SNOOP_STATS 1024
static SnoopStat stats[MAX_SNOOP_STATS]; // Static stat storage
static int stat_count = 0;              // Current tracked distinct syscall count

// Look up name for syscall number or format fallback
static void get_syscall_name_str(long sc_num, char *buf, size_t max_len) {
    if (sc_num >= 0 && sc_num < SYSCALL_MAX && syscall_names[sc_num] != NULL) {
        strncpy(buf, syscall_names[sc_num], max_len - 1);
        buf[max_len - 1] = '\0';
    } else { // Fallback generic name
        snprintf(buf, max_len, "syscall_%ld", sc_num);
    }
}

// Record an invocation and elapsed time for a syscall
static void record_syscall(long sc_num, double elapsed) {
    for (int i = 0; i < stat_count; i++) { // Check existing entry
        if (stats[i].sc_num == sc_num) {
            stats[i].calls++;
            stats[i].total_time += elapsed;
            return;
        }
    }
    // Allocate new tracking entry
    if (stat_count < MAX_SNOOP_STATS) {
        stats[stat_count].sc_num = sc_num;
        get_syscall_name_str(sc_num, stats[stat_count].name, sizeof(stats[stat_count].name));
        stats[stat_count].calls = 1;
        stats[stat_count].total_time = elapsed;
        stats[stat_count].first_order = stat_count;
        stat_count++;
    }
}

// Sort comparator: descending calls, ties broken by first appearance
static int compare_snoop_stats(const void *a, const void *b) {
    const SnoopStat *sa = (const SnoopStat *)a;
    const SnoopStat *sb = (const SnoopStat *)b;
    if (sb->calls != sa->calls) {
        return sb->calls - sa->calls; // Descending by call count
    }
    return sa->first_order - sb->first_order; // Ascending by first seen order
}

// Builtin command: snoop <command> or snoop -p <pid>
int builtin_snoop(Command *cmd) {
    if (cmd == NULL || cmd->argc < 2) { // Validate command arguments
        fprintf(stderr, "snoop: command not found\n");
        return 1;
    }

    stat_count = 0; // Reset metrics
    pid_t target_pid = -1;
    bool is_attach = false; // Mode flag: attach vs spawn

    // Block SIGCHLD while snoop runs so the shell's async SIGCHLD handler doesn't steal trace events
    sigset_t chld_mask, old_mask;
    sigemptyset(&chld_mask);
    sigaddset(&chld_mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &chld_mask, &old_mask);

    // Parse -p <pid> attach flag
    if (strcmp(cmd->args[1], "-p") == 0) {
        if (cmd->argc < 3) {
            fprintf(stderr, "snoop: no such process\n");
            sigprocmask(SIG_SETMASK, &old_mask, NULL);
            return 1;
        }
        for (int i = 0; cmd->args[2][i] != '\0'; i++) { // Ensure PID is all digits
            if (!isdigit((unsigned char)cmd->args[2][i])) {
                fprintf(stderr, "snoop: no such process\n");
                sigprocmask(SIG_SETMASK, &old_mask, NULL);
                return 1;
            }
        }
        target_pid = (pid_t)atoi(cmd->args[2]); // Parse target pid
        if (target_pid <= 0 || kill(target_pid, 0) != 0) { // Verify process exists
            fprintf(stderr, "snoop: no such process\n");
            sigprocmask(SIG_SETMASK, &old_mask, NULL);
            return 1;
        }
        if (ptrace(PTRACE_ATTACH, target_pid, NULL, NULL) == -1) { // Attach to process
            fprintf(stderr, "snoop: no such process\n");
            sigprocmask(SIG_SETMASK, &old_mask, NULL);
            return 1;
        }
        is_attach = true; // Mark attached
        int status;
        waitpid(target_pid, &status, 0); // Wait for initial stop
    } else {
        // Spawn mode: resolve binary path
        char resolved_path[MAX_PATH];
        if (!resolve_command(cmd->args[1], resolved_path, sizeof(resolved_path))) {
            fprintf(stderr, "snoop: command not found\n");
            sigprocmask(SIG_SETMASK, &old_mask, NULL);
            return 1;
        }

        // Fork child to execute command
        target_pid = fork();
        if (target_pid < 0) {
            perror("fork");
            sigprocmask(SIG_SETMASK, &old_mask, NULL);
            return 1;
        }

        if (target_pid == 0) {
            // Child process: unblock SIGCHLD, request tracing and halt
            sigprocmask(SIG_SETMASK, &old_mask, NULL);
            ptrace(PTRACE_TRACEME, 0, NULL, NULL);
            raise(SIGSTOP); // Stop until parent configures ptrace
            execv(resolved_path, &cmd->args[1]); // Execute target binary
            perror("execv");
            exit(127);
        }

        // Parent: wait for child SIGSTOP
        int status;
        waitpid(target_pid, &status, 0);
    }

    // Set ptrace options: distinguish syscall traps with bit 7
    ptrace(PTRACE_SETOPTIONS, target_pid, 0, PTRACE_O_TRACESYSGOOD);
    ptrace(PTRACE_SYSCALL, target_pid, NULL, NULL); // Resume until next syscall

    bool in_syscall = false;            // Syscall entry vs exit state
    long current_sc = -1;               // Current syscall number
    struct timespec start_time = {0, 0}; // Monotonic start timestamp

    // Trace loop
    while (true) {
        int status;
        pid_t res = waitpid(target_pid, &status, 0); // Wait for tracee event
        if (res <= 0) break; // Break on error or non-positive result

        if (WIFEXITED(status) || WIFSIGNALED(status)) { // Child terminated
            break;
        }

        if (WIFSTOPPED(status)) { // Child stopped
            int sig = WSTOPSIG(status);

            if (sig == (SIGTRAP | 0x80)) { // Syscall stop indicated by TRACESYSGOOD
                struct user_regs_struct regs;
                if (ptrace(PTRACE_GETREGS, target_pid, NULL, &regs) == 0) {
                    long sc_num = (long)regs.orig_rax; // Read syscall number from orig_rax
                    if (!in_syscall) { // Syscall entry
                        in_syscall = true;
                        current_sc = sc_num;
                        clock_gettime(CLOCK_MONOTONIC, &start_time); // Record start time
                    } else { // Syscall exit
                        in_syscall = false;
                        struct timespec end_time;
                        clock_gettime(CLOCK_MONOTONIC, &end_time); // Record end time
                        double elapsed = (end_time.tv_sec - start_time.tv_sec) +
                                         (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
                        if (elapsed < 0) elapsed = 0; // Guard against clock skew
                        record_syscall(current_sc, elapsed); // Store measurement
                    }
                }
                ptrace(PTRACE_SYSCALL, target_pid, NULL, NULL); // Continue to next syscall
            } else {
                // Forward other signals, suppressing internal ptrace stops
                int deliver_sig = (sig == SIGSTOP || sig == SIGTRAP) ? 0 : sig;
                ptrace(PTRACE_SYSCALL, target_pid, NULL, (void *)(intptr_t)deliver_sig);
            }
        }
    }

    // Detach if attached to external PID
    if (is_attach) {
        ptrace(PTRACE_DETACH, target_pid, NULL, NULL);
    }

    // Unblock SIGCHLD after tracee loop completes
    sigprocmask(SIG_SETMASK, &old_mask, NULL);

    // Sort summary table: call count descending, break ties by first occurrence
    qsort(stats, stat_count, sizeof(SnoopStat), compare_snoop_stats);

    // Print summary table
    printf("%-13s %-7s %s\n", "syscall", "calls", "time");
    for (int i = 0; i < stat_count; i++) {
        printf("%-13s %-7d %.3fs\n", stats[i].name, stats[i].calls, stats[i].total_time);
    }

    return 0; // Success // Success
}
