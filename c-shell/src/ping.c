#include "ping.h"
#include "jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>

static bool is_valid_non_negative_int(const char *str) {
    if (str == NULL || *str == '\0') return false;
    for (int i = 0; str[i] != '\0'; i++) {
        if (!isdigit((unsigned char)str[i])) {
            return false;
        }
    }
    return true;
}

int builtin_ping(Command *cmd) {
    if (cmd == NULL || cmd->argc != 3) {
        fprintf(stderr, "ping: invalid syntax\n");
        return 1;
    }

    const char *target = cmd->args[1];
    const char *sig_str = cmd->args[2];

    // Validate signal_number before looking up target
    if (!is_valid_non_negative_int(sig_str)) {
        fprintf(stderr, "ping: invalid syntax\n");
        return 1;
    }

    int raw_sig = atoi(sig_str);
    int sig = raw_sig % 64;

    sync_job_states();

    if (target[0] == '%') {
        if (!is_valid_non_negative_int(target + 1)) {
            fprintf(stderr, "ping: no such process found\n");
            return 1;
        }
        int job_num = atoi(target + 1);
        TrackedJob *job = get_job_by_number(job_num);
        if (job == NULL || !job->active) {
            fprintf(stderr, "ping: no such process found\n");
            return 1;
        }

        // Send signal to every process in that job's group
        kill(-job->pgid, sig);
        printf("Sent signal %d to %s\n", raw_sig, target);
        return 0;
    } else {
        if (!is_valid_non_negative_int(target)) {
            fprintf(stderr, "ping: no such process found\n");
            return 1;
        }
        pid_t pid = (pid_t)atoi(target);
        int proc_idx = -1;
        TrackedJob *job = get_job_by_pid(pid, &proc_idx);
        if (job == NULL || !job->active || proc_idx < 0) {
            fprintf(stderr, "ping: no such process found\n");
            return 1;
        }

        // Send signal to individual process
        kill(pid, sig);
        printf("Sent signal %d to %s\n", raw_sig, target);
        return 0;
    }
}
