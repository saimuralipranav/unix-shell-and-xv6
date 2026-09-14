#include "ping.h"
#include "jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>

// Validates that a string is a non-empty sequence of digits
static bool is_valid_non_negative_int(const char *str) {
    if (str == NULL || *str == '\0') return false; // empty string check
    // verify every character is a digit
    for (int i = 0; str[i] != '\0'; i++) {
        if (!isdigit((unsigned char)str[i])) {
            return false; // non-digit found
        }
    }
    return true; // valid non-negative integer
}

// Builtin 'ping': sends a signal to a process or job group
int builtin_ping(Command *cmd) {
    // Exactly 3 arguments required: ping <target> <signal_number>
    if (cmd == NULL || cmd->argc != 3) {
        fprintf(stderr, "ping: invalid syntax\n"); // invalid syntax
        return 1;
    }

    const char *target = cmd->args[1];  // process PID or %job_number
    const char *sig_str = cmd->args[2]; // signal number string

    // Validate signal_number before looking up target
    if (!is_valid_non_negative_int(sig_str)) {
        fprintf(stderr, "ping: invalid syntax\n"); // signal must be non-negative integer
        return 1;
    }

    int raw_sig = atoi(sig_str); // parse raw signal number
    int sig = raw_sig % 64;      // apply modulo 64 per spec

    // Refresh tracked jobs status
    sync_job_states();

    // Handle job group target (%job_number)
    if (target[0] == '%') {
        // validate job number digits
        if (!is_valid_non_negative_int(target + 1)) {
            fprintf(stderr, "ping: no such process found\n");
            return 1;
        }
        int job_num = atoi(target + 1);             // parse job number
        TrackedJob *job = get_job_by_number(job_num); // lookup job
        if (job == NULL || !job->active) {
            fprintf(stderr, "ping: no such process found\n"); // unknown job
            return 1;
        }

        // Send signal to every process in that job's group using negative PGID
        kill(-job->pgid, sig);
        printf("Sent signal %d to %s\n", raw_sig, target); // echo original typed signal
        return 0;
    } else {
        // Handle individual process target (pid)
        if (!is_valid_non_negative_int(target)) {
            fprintf(stderr, "ping: no such process found\n");
            return 1;
        }
        pid_t pid = (pid_t)atoi(target); // parse target pid
        int proc_idx = -1;               // process index within job
        TrackedJob *job = get_job_by_pid(pid, &proc_idx); // verify shell spawned this pid
        if (job == NULL || !job->active || proc_idx < 0) {
            fprintf(stderr, "ping: no such process found\n"); // unmanaged pid
            return 1;
        }

        // Send signal to individual process
        kill(pid, sig);
        printf("Sent signal %d to %s\n", raw_sig, target); // echo original typed signal
        return 0;
    }
}
