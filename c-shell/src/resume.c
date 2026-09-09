#include "resume.h"
#include "jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

static volatile sig_atomic_t timeout_expired = 0;

static void handle_sigalrm(int sig) {
    (void)sig;
    timeout_expired = 1;
}

int builtin_resume(Command *cmd) {
    if (cmd == NULL || cmd->argc < 3) {
        fprintf(stderr, "resume: invalid syntax\n");
        return 1;
    }

    // Must start with %
    if (cmd->args[1][0] != '%') {
        fprintf(stderr, "resume: invalid syntax\n");
        return 1;
    }

    char *endptr = NULL;
    long job_num = strtol(cmd->args[1] + 1, &endptr, 10);
    if (*endptr != '\0' || job_num <= 0) {
        fprintf(stderr, "resume: invalid syntax\n");
        return 1;
    }

    sync_job_states();

    TrackedJob *job = get_job_by_number((int)job_num);
    if (job == NULL || !job->active) {
        fprintf(stderr, "resume: no such job\n");
        return 1;
    }

    const char *mode = cmd->args[2];
    if (strcmp(mode, "bg") == 0) {
        if (cmd->argc != 3) {
            fprintf(stderr, "resume: invalid syntax\n");
            return 1;
        }

        // Send SIGCONT to the process group
        kill(-job->pgid, SIGCONT);

        for (int p = 0; p < job->proc_count; p++) {
            job->procs[p].state = PROC_RUNNING;
        }
        job->is_background = true;

        printf("[%d] + Running    %s\n", job->job_number, job->cmd_line);
        return 0;
    } else if (strcmp(mode, "fg") == 0) {
        int timeout_sec = 0;
        if (cmd->argc > 3) {
            if (cmd->argc != 5 || strcmp(cmd->args[3], "--timeout") != 0) {
                fprintf(stderr, "resume: invalid syntax\n");
                return 1;
            }
            char *tend = NULL;
            long val = strtol(cmd->args[4], &tend, 10);
            if (*tend != '\0' || val <= 0) {
                fprintf(stderr, "resume: invalid syntax\n");
                return 1;
            }
            timeout_sec = (int)val;
        }

        // 9. fg: print command line
        printf("%s\n", job->cmd_line);
        fflush(stdout);

        pid_t shell_pgid = getpgrp();

        // Give job the terminal
        if (isatty(STDIN_FILENO)) {
            tcsetpgrp(STDIN_FILENO, job->pgid);
        }

        // Send SIGCONT to process group
        kill(-job->pgid, SIGCONT);

        for (int p = 0; p < job->proc_count; p++) {
            job->procs[p].state = PROC_RUNNING;
        }

        struct sigaction sa, old_sa;
        if (timeout_sec > 0) {
            timeout_expired = 0;
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = handle_sigalrm;
            sigaction(SIGALRM, &sa, &old_sa);
            alarm((unsigned int)timeout_sec);
        }

        bool stopped = false;
        bool timed_out = false;

        // Wait on the processes in the job
        for (int p = 0; p < job->proc_count; p++) {
            pid_t p_pid = job->procs[p].pid;
            int status = 0;

            while (true) {
                pid_t res = waitpid(p_pid, &status, WUNTRACED);
                if (res > 0) {
                    if (WIFSTOPPED(status)) {
                        stopped = true;
                        job->procs[p].state = PROC_STOPPED;
                    } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
                        job->procs[p].state = PROC_EXITED;
                    }
                    break;
                } else if (res < 0) {
                    if (errno == EINTR) {
                        if (timeout_expired) {
                            timed_out = true;
                            kill(-job->pgid, SIGTERM);
                            break;
                        }
                        continue;
                    }
                    break; // No child or other error
                }
            }

            if (timed_out) break;
        }

        if (timeout_sec > 0) {
            alarm(0);
            sigaction(SIGALRM, &old_sa, NULL);
        }

        // Reclaim terminal
        if (isatty(STDIN_FILENO)) {
            tcsetpgrp(STDIN_FILENO, shell_pgid);
        }

        if (timed_out) {
            printf("resume: job timed out\n");
            job->active = false;
            if (job->procs != NULL) {
                free(job->procs);
                job->procs = NULL;
                job->proc_count = 0;
            }
            // Reap any children that exited after SIGTERM
            int st;
            while (waitpid(-job->pgid, &st, WNOHANG) > 0);
            return 0;
        }

        if (stopped) {
            for (int p = 0; p < job->proc_count; p++) {
                if (job->procs[p].state != PROC_EXITED) {
                    job->procs[p].state = PROC_STOPPED;
                }
            }
            job->is_background = true;
            printf("[%d] + Stopped    %s\n", job->job_number, job->cmd_line);
        } else {
            // Check if all exited
            sync_job_states();
        }

        return 0;
    } else {
        fprintf(stderr, "resume: invalid syntax\n");
        return 1;
    }
}
