#include "resume.h"
#include "jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

static volatile sig_atomic_t timeout_expired = 0; // timeout expired flag

// Signal handler for SIGALRM timeout
static void handle_sigalrm(int sig) {
    (void)sig;            // unused signal number
    timeout_expired = 1;  // mark timeout reached
}

// Builtin 'resume': resumes a stopped/background job in fg or bg
int builtin_resume(Command *cmd) {
    // Check minimum argument count: resume %job (fg|bg)
    if (cmd == NULL || cmd->argc < 3) {
        fprintf(stderr, "resume: invalid syntax\n"); // invalid syntax
        return 1;
    }

    // Job number must be prefixed with '%'
    if (cmd->args[1][0] != '%') {
        fprintf(stderr, "resume: invalid syntax\n"); // missing '%' prefix
        return 1;
    }

    char *endptr = NULL;
    // parse job number following '%'
    long job_num = strtol(cmd->args[1] + 1, &endptr, 10);
    if (*endptr != '\0' || job_num <= 0) {
        fprintf(stderr, "resume: invalid syntax\n"); // invalid number
        return 1;
    }

    const char *mode = cmd->args[2]; // target mode: fg or bg
    int timeout_sec = 0;             // timeout duration in seconds

    // Validate mode and timeout flags
    if (strcmp(mode, "bg") == 0) {
        // bg mode takes no additional arguments
        if (cmd->argc != 3) {
            fprintf(stderr, "resume: invalid syntax\n");
            return 1;
        }
    } else if (strcmp(mode, "fg") == 0) {
        // fg mode optionally takes --timeout <seconds>
        if (cmd->argc > 3) {
            // verify exact format: fg --timeout <seconds>
            if (cmd->argc != 5 || strcmp(cmd->args[3], "--timeout") != 0) {
                fprintf(stderr, "resume: invalid syntax\n");
                return 1;
            }
            char *tend = NULL;
            // parse timeout integer value
            long val = strtol(cmd->args[4], &tend, 10);
            if (*tend != '\0' || val <= 0) {
                fprintf(stderr, "resume: invalid syntax\n"); // timeout must be positive
                return 1;
            }
            timeout_sec = (int)val; // set timeout seconds
        }
    } else {
        fprintf(stderr, "resume: invalid syntax\n"); // unknown mode
        return 1;
    }

    // Synchronize current job states
    sync_job_states();

    // Look up specified job by number
    TrackedJob *job = get_job_by_number((int)job_num);
    if (job == NULL || !job->active) {
        fprintf(stderr, "resume: no such job\n"); // job not found or dead
        return 1;
    }

    if (strcmp(mode, "bg") == 0) {
        // Send SIGCONT to the process group to resume in background
        kill(-job->pgid, SIGCONT);

        // Update each process state to Running
        for (int p = 0; p < job->proc_count; p++) {
            job->procs[p].state = PROC_RUNNING;
        }
        job->is_background = true; // mark job as background

        // Print job continuation notice per spec
        printf("[%d] + Running    %s\n", job->job_number, job->cmd_line);
        return 0; // return to shell prompt immediately
    } else {
        // Foreground (fg) resumption mode

        // Echo command line before waiting
        printf("%s\n", job->cmd_line);
        fflush(stdout); // flush output

        pid_t shell_pgid = getpgrp(); // save shell process group

        // Hand terminal control over to the job
        if (isatty(STDIN_FILENO)) {
            tcsetpgrp(STDIN_FILENO, job->pgid);
        }

        // Send SIGCONT to the job process group
        kill(-job->pgid, SIGCONT);

        // Mark all processes in job as Running
        for (int p = 0; p < job->proc_count; p++) {
            job->procs[p].state = PROC_RUNNING;
        }

        // Configure timer if --timeout was requested
        struct sigaction sa, old_sa;
        if (timeout_sec > 0) {
            timeout_expired = 0; // reset flag
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = handle_sigalrm; // attach handler
            sigaction(SIGALRM, &sa, &old_sa);
            alarm((unsigned int)timeout_sec); // set alarm timer
        }

        bool stopped = false;   // stopped state flag
        bool timed_out = false; // timed out state flag

        // Wait on all processes in the resumed job
        for (int p = 0; p < job->proc_count; p++) {
            pid_t p_pid = job->procs[p].pid; // process PID
            int status = 0;                  // wait status

            while (true) {
                pid_t res = waitpid(p_pid, &status, WUNTRACED); // wait for state change
                if (res > 0) {
                    if (WIFSTOPPED(status)) {
                        stopped = true; // process was stopped (Ctrl-Z)
                        job->procs[p].state = PROC_STOPPED;
                    } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
                        job->procs[p].state = PROC_EXITED; // process terminated
                    }
                    break;
                } else if (res < 0) {
                    if (errno == EINTR) {
                        // Check if interrupted by SIGALRM timeout
                        if (timeout_expired) {
                            timed_out = true;
                            kill(-job->pgid, SIGTERM); // terminate timed-out job
                            break;
                        }
                        continue; // restart wait if interrupted by other signal
                    }
                    break; // child no longer exists or error
                }
            }

            if (timed_out) break; // abort loop on timeout
        }

        // Cancel pending alarm and restore previous handler
        if (timeout_sec > 0) {
            alarm(0);
            sigaction(SIGALRM, &old_sa, NULL);
        }

        // Reclaim terminal control back to the shell
        if (isatty(STDIN_FILENO)) {
            tcsetpgrp(STDIN_FILENO, shell_pgid);
        }

        // Handle timeout expiration
        if (timed_out) {
            printf("resume: job timed out\n");
            job->active = false; // remove job from tracked list
            if (job->procs != NULL) {
                free(job->procs); // free process table
                job->procs = NULL;
                job->proc_count = 0;
            }
            // Reap any children that exited after SIGTERM
            int st;
            while (waitpid(-job->pgid, &st, WNOHANG) > 0);
            return 0;
        }

        // Handle process stopped by signal (e.g. Ctrl-Z)
        if (stopped) {
            for (int p = 0; p < job->proc_count; p++) {
                if (job->procs[p].state != PROC_EXITED) {
                    job->procs[p].state = PROC_STOPPED;
                }
            }
            job->is_background = true; // move to background
            printf("[%d] + Stopped    %s\n", job->job_number, job->cmd_line);
        } else {
            // Check if all processes in job exited
            sync_job_states();
        }

        return 0; // return success
    }
}
