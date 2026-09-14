#include "exec.h"
#include "builtins.h"
#include "jobs.h"
#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>

// ---------------------------------------------------------
// Helper: Check if file is executable
// ---------------------------------------------------------
// Helper: Checks if file exists and has executable permissions
static bool is_executable(const char *path) {
    struct stat st; // stat structure
    // query file status
    if (stat(path, &st) == 0) {
        // regular file check and user execution permission check
        if (S_ISREG(st.st_mode) && (access(path, X_OK) == 0)) {
            return true; // executable file
        }
    }
    return false; // not executable
}

// Resolves command path according to C1 rules (CWD first, % skips CWD, searches PATH)
bool resolve_command(const char *cmd_name, char *out_path, size_t max_len) {
    if (cmd_name == NULL || strlen(cmd_name) == 0) return false; // empty name check

    // Rule 1: contains '/' -> treat as literal relative or absolute path
    if (strchr(cmd_name, '/') != NULL) {
        strncpy(out_path, cmd_name, max_len - 1); // copy path
        out_path[max_len - 1] = '\0';             // null-terminate
        return is_executable(out_path);          // check executable
    }

    // Rule 3: starts with '%' -> skip CWD check and search PATH directly
    if (cmd_name[0] == '%') {
        const char *real_name = cmd_name + 1; // skip leading '%'
        const char *path_env = getenv("PATH");// query PATH
        if (path_env != NULL) {
            char *path_copy = strdup(path_env); // duplicate for tokenizing
            if (path_copy != NULL) {
                char *dir = strtok(path_copy, ":"); // tokenize PATH entries
                while (dir != NULL) {
                    char full_path[MAX_PATH];
                    // empty PATH entry represents cwd
                    if (strlen(dir) == 0) {
                        snprintf(full_path, sizeof(full_path), "./%s", real_name);
                    } else {
                        snprintf(full_path, sizeof(full_path), "%s/%s", dir, real_name);
                    }
                    // verify executable
                    if (is_executable(full_path)) {
                        strncpy(out_path, full_path, max_len - 1);
                        out_path[max_len - 1] = '\0';
                        free(path_copy); // free duplicated string
                        return true;     // resolved
                    }
                    dir = strtok(NULL, ":"); // next PATH entry
                }
                free(path_copy); // free duplicated string
            }
        }
        return false; // not found in PATH
    }

    // Rule 2: check current working directory first
    char cwd_path[MAX_PATH];
    snprintf(cwd_path, sizeof(cwd_path), "./%s", cmd_name); // construct CWD path
    if (is_executable(cwd_path)) {
        strncpy(out_path, cwd_path, max_len - 1);
        out_path[max_len - 1] = '\0';
        return true; // found in CWD
    }

    // Rule 2 continued: search PATH if not found in CWD
    const char *path_env = getenv("PATH"); // query PATH
    if (path_env != NULL) {
        char *path_copy = strdup(path_env); // duplicate for tokenizing
        if (path_copy != NULL) {
            char *dir = strtok(path_copy, ":"); // tokenize PATH
            while (dir != NULL) {
                char full_path[MAX_PATH];
                if (strlen(dir) == 0) {
                    snprintf(full_path, sizeof(full_path), "./%s", cmd_name);
                } else {
                    snprintf(full_path, sizeof(full_path), "%s/%s", dir, cmd_name);
                }
                // verify executable
                if (is_executable(full_path)) {
                    strncpy(out_path, full_path, max_len - 1);
                    out_path[max_len - 1] = '\0';
                    free(path_copy); // free memory
                    return true;     // resolved
                }
                dir = strtok(NULL, ":"); // next entry
            }
            free(path_copy); // free memory
        }
    }

    return false; // command not found
}

// Verifies all input redirection files exist and can be opened for reading
static bool verify_input_files(Command *cmd) {
    for (int i = 0; i < cmd->in_redir_count; i++) {
        int fd = open(cmd->in_redirs[i].filepath, O_RDONLY); // try opening
        if (fd < 0) {
            fprintf(stderr, "cshell: no such file or directory\n"); // report error
            return false; // fail verification
        }
        close(fd); // close test descriptor
    }
    return true; // all input files valid
}

// Verifies all output redirection files can be created or opened for writing
static bool verify_output_files(Command *cmd) {
    for (int i = 0; i < cmd->out_redir_count; i++) {
        // determine mode: O_APPEND vs O_TRUNC with permissions 0644
        int flags = O_WRONLY | O_CREAT | (cmd->out_redirs[i].append ? O_APPEND : O_TRUNC);
        int fd = open(cmd->out_redirs[i].filepath, flags, 0644); // test open/creation
        if (fd < 0) {
            fprintf(stderr, "cshell: unable to create file for writing\n"); // report error
            return false; // fail verification
        }
        close(fd); // close test descriptor
    }
    return true; // all output files valid
}

// Helper process: concatenates multiple input files into a single pipe write end
static void run_input_helper(Command *cmd, int write_fd) {
    // read each input file in order
    for (int i = 0; i < cmd->in_redir_count; i++) {
        int fd = open(cmd->in_redirs[i].filepath, O_RDONLY); // open input file
        if (fd >= 0) {
            char buf[4096]; // chunk buffer
            ssize_t r;
            // stream bytes to pipe write end
            while ((r = read(fd, buf, sizeof(buf))) > 0) {
                ssize_t written = 0;
                while (written < r) {
                    ssize_t w = write(write_fd, buf + written, r - written);
                    if (w < 0) break;
                    written += w;
                }
            }
            close(fd); // close file
        }
    }
}

// Helper process: duplicates output from pipe read end into multiple target files
static void run_output_helper(Command *cmd, int read_fd) {
    int *fds = malloc(cmd->out_redir_count * sizeof(int)); // output fd array
    if (fds == NULL) {
        perror("malloc");
        return;
    }
    // open all target output files
    for (int i = 0; i < cmd->out_redir_count; i++) {
        int flags = O_WRONLY | O_CREAT | (cmd->out_redirs[i].append ? O_APPEND : O_TRUNC);
        fds[i] = open(cmd->out_redirs[i].filepath, flags, 0644); // open file
    }

    char buf[4096]; // chunk buffer
    ssize_t r;
    // stream bytes to all open output files simultaneously
    while ((r = read(read_fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < cmd->out_redir_count; i++) {
            if (fds[i] >= 0) {
                ssize_t written = 0;
                while (written < r) {
                    ssize_t w = write(fds[i], buf + written, r - written);
                    if (w < 0) break;
                    written += w;
                }
            }
        }
    }

    // close all output file descriptors
    for (int i = 0; i < cmd->out_redir_count; i++) {
        if (fds[i] >= 0) {
            close(fds[i]);
        }
    }
    free(fds); // free array
}

// Formats a pipeline into a readable command-line string for job tracking
static void format_pipeline_cmdline(Pipeline *pipeline, char *out, size_t max_len) {
    out[0] = '\0'; // clear output buffer
    for (int i = 0; i < pipeline->cmd_count; i++) {
        // separate pipe stages with " | "
        if (i > 0) {
            strncat(out, " | ", max_len - strlen(out) - 1);
        }
        // append command arguments separated by spaces
        for (int a = 0; a < pipeline->cmds[i].argc; a++) {
            if (a > 0) strncat(out, " ", max_len - strlen(out) - 1);
            strncat(out, pipeline->cmds[i].args[a], max_len - strlen(out) - 1);
        }
    }
}

// Hook to reap background jobs asynchronously
void check_background_jobs(void) {
    reap_background_jobs(); // non-blocking reap
}

// ---------------------------------------------------------
// Pipeline Execution
// Core execution loop: verifies files, forks processes, sets up pipes,
// handles redirections, and executes builtins or external commands.
// Also manages process groups for foreground/background execution.
// ---------------------------------------------------------
// Executes a pipeline of one or more commands connected by pipes
int execute_pipeline(Pipeline *pipeline, bool *failed_to_start) {
    if (pipeline == NULL || pipeline->cmd_count == 0) return 0; // empty check
    if (failed_to_start) *failed_to_start = false;              // initialize flag

    int cmd_count = pipeline->cmd_count; // number of commands

    // Verify all input and output redirection files for all stages
    for (int i = 0; i < cmd_count; i++) {
        if (!verify_input_files(&pipeline->cmds[i]) || !verify_output_files(&pipeline->cmds[i])) {
            return 1; // redirection setup failed
        }
    }

    // Special case: Single command
    if (cmd_count == 1) {
        Command *cmd = &pipeline->cmds[0];
        char resolved_path[MAX_PATH];
        
        bool is_b = is_builtin(cmd->args[0]); // check if builtin
        // If not a builtin, verify executable exists
        if (!is_b && !resolve_command(cmd->args[0], resolved_path, sizeof(resolved_path))) {
            const char *print_name = (cmd->args[0][0] == '%') ? (cmd->args[0] + 1) : cmd->args[0];
            fprintf(stderr, "cshell: command not found (%s)\n", print_name); // report command not found
            if (failed_to_start) *failed_to_start = true; // flag failure to start
            return -2;
        }

        // Execute foreground builtins directly in the parent shell process
        if (is_b && !pipeline->is_background) {
            int saved_in = dup(STDIN_FILENO);   // save original stdin
            int saved_out = dup(STDOUT_FILENO); // save original stdout

            pid_t helper_in = -1;  // input helper PID
            pid_t helper_out = -1; // output helper PID

            // Setup input redirection for builtin
            if (cmd->in_redir_count == 1) {
                int fd = open(cmd->in_redirs[0].filepath, O_RDONLY);
                if (fd >= 0) { dup2(fd, STDIN_FILENO); close(fd); }
            } else if (cmd->in_redir_count > 1) {
                int in_p[2];
                if (pipe(in_p) == 0) {
                    helper_in = fork();
                    if (helper_in == 0) {
                        close(in_p[0]); run_input_helper(cmd, in_p[1]); close(in_p[1]); exit(0);
                    }
                    close(in_p[1]); dup2(in_p[0], STDIN_FILENO); close(in_p[0]);
                }
            }

            // Setup output redirection for builtin
            if (cmd->out_redir_count == 1) {
                int flags = O_WRONLY | O_CREAT | (cmd->out_redirs[0].append ? O_APPEND : O_TRUNC);
                int fd = open(cmd->out_redirs[0].filepath, flags, 0644);
                if (fd >= 0) { dup2(fd, STDOUT_FILENO); close(fd); }
            } else if (cmd->out_redir_count > 1) {
                int out_p[2];
                if (pipe(out_p) == 0) {
                    helper_out = fork();
                    if (helper_out == 0) {
                        close(out_p[1]); run_output_helper(cmd, out_p[0]); close(out_p[0]); exit(0);
                    }
                    close(out_p[0]); dup2(out_p[1], STDOUT_FILENO); close(out_p[1]);
                }
            }

            int status = exec_builtin(cmd); // run builtin

            // Restore standard streams
            dup2(saved_in, STDIN_FILENO); close(saved_in);
            dup2(saved_out, STDOUT_FILENO); close(saved_out);

            // Wait for helper processes
            if (helper_in > 0) waitpid(helper_in, NULL, 0);
            if (helper_out > 0) waitpid(helper_out, NULL, 0);
            return status; // return builtin status
        }
    }

    // Allocate pipe file descriptors for multi-stage pipelines
    int (*pipes)[2] = NULL;
    if (cmd_count > 1) {
        pipes = malloc((cmd_count - 1) * sizeof(*pipes));
        if (pipes == NULL) { perror("malloc"); return 1; }
        for (int i = 0; i < cmd_count - 1; i++) {
            if (pipe(pipes[i]) < 0) {
                perror("pipe"); free(pipes); return 1;
            }
        }
    }

    // Allocate PID tracking array
    pid_t *pids = malloc(cmd_count * sizeof(pid_t));
    if (pids == NULL) {
        perror("malloc");
        if (pipes) free(pipes);
        return 1;
    }

    // Fork and setup each pipeline stage
    for (int i = 0; i < cmd_count; i++) {
        Command *cmd = &pipeline->cmds[i];
        pids[i] = fork(); // fork child process

        if (pids[i] == 0) {
            // Child: assign process group (leader is first child)
            setpgid(0, (i == 0 ? 0 : pids[0]));

            // Reset signal dispositions for child process
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);

            // Wire up pipeline pipes
            if (i > 0) dup2(pipes[i - 1][0], STDIN_FILENO);         // read from previous pipe
            if (i < cmd_count - 1) dup2(pipes[i][1], STDOUT_FILENO); // write to next pipe

            // Close all inherited pipe descriptors in child
            for (int p = 0; p < cmd_count - 1; p++) {
                close(pipes[p][0]); close(pipes[p][1]);
            }

            // Apply file input redirections (overrides pipe if specified)
            if (cmd->in_redir_count == 1) {
                int fd = open(cmd->in_redirs[0].filepath, O_RDONLY);
                if (fd >= 0) { dup2(fd, STDIN_FILENO); close(fd); }
            } else if (cmd->in_redir_count > 1) {
                int in_p[2];
                if (pipe(in_p) == 0) {
                    if (fork() == 0) { close(in_p[0]); run_input_helper(cmd, in_p[1]); close(in_p[1]); exit(0); }
                    close(in_p[1]); dup2(in_p[0], STDIN_FILENO); close(in_p[0]);
                }
            }

            // Apply file output redirections (overrides pipe if specified)
            if (cmd->out_redir_count == 1) {
                int flags = O_WRONLY | O_CREAT | (cmd->out_redirs[0].append ? O_APPEND : O_TRUNC);
                int fd = open(cmd->out_redirs[0].filepath, flags, 0644);
                if (fd >= 0) { dup2(fd, STDOUT_FILENO); close(fd); }
            } else if (cmd->out_redir_count > 1) {
                int out_p[2];
                if (pipe(out_p) == 0) {
                    if (fork() == 0) { close(out_p[1]); run_output_helper(cmd, out_p[0]); close(out_p[0]); exit(0); }
                    close(out_p[0]); dup2(out_p[1], STDOUT_FILENO); close(out_p[1]);
                }
            }

            // Execute builtin or external executable
            if (is_builtin(cmd->args[0])) {
                exit(exec_builtin(cmd)); // exit child with builtin result
            } else {
                char resolved_path[MAX_PATH];
                if (resolve_command(cmd->args[0], resolved_path, sizeof(resolved_path))) {
                    if (cmd->args[0][0] == '%') cmd->args[0]++; // strip '%' for argv
                    execv(resolved_path, cmd->args);             // execute binary
                    perror("execv");                             // exec failure
                    exit(1);
                } else {
                    const char *print_name = (cmd->args[0][0] == '%') ? (cmd->args[0] + 1) : cmd->args[0];
                    fprintf(stderr, "cshell: command not found (%s)\n", print_name);
                    exit(127);
                }
            }
        } else if (pids[i] > 0) {
            // Parent: set child's process group immediately after fork
            setpgid(pids[i], (i == 0 ? pids[0] : pids[0]));
        }
    }

    // Parent closes all pipe descriptors
    for (int p = 0; p < cmd_count - 1; p++) {
        close(pipes[p][0]); close(pipes[p][1]);
    }

    // Background pipeline execution
    if (pipeline->is_background) {
        char cmd_line[1024];
        format_pipeline_cmdline(pipeline, cmd_line, sizeof(cmd_line)); // format command line
        TrackedJob *job = create_job(pids[0], cmd_line, true);          // register background job
        if (job != NULL) {
            for (int i = 0; i < cmd_count; i++) {
                job_add_process(job, pids[i], pipeline->cmds[i].args[0]); // record processes
            }
            printf("[%d] %d\n", job->job_number, pids[0]); // print [job] pid per spec
            fflush(stdout);                                // flush output
        }
        if (pipes) free(pipes); // free pipe memory
        free(pids);             // free PID array
        return 0;               // return immediately for background jobs
    }

    // Foreground execution: hand terminal over to the child process group
    pid_t shell_pgid = getpgrp(); // shell PGID
    if (isatty(STDIN_FILENO)) {
        tcsetpgrp(STDIN_FILENO, pids[0]); // give terminal control
    }

    int last_status = 0; // exit status of last pipeline stage
    bool stopped = false;// stopped flag

    // Wait on each command in the foreground pipeline
    for (int i = 0; i < cmd_count; i++) {
        int status;
        if (waitpid(pids[i], &status, WUNTRACED) == pids[i]) {
            if (WIFSTOPPED(status)) {
                stopped = true; // Ctrl-Z intercepted
            } else if (i == cmd_count - 1) {
                // record exit status of the final pipeline stage
                if (WIFEXITED(status)) last_status = WEXITSTATUS(status);
                else if (WIFSIGNALED(status)) last_status = 128 + WTERMSIG(status);
            }
        }
    }

    // Reclaim terminal control back to the shell
    if (isatty(STDIN_FILENO)) {
        tcsetpgrp(STDIN_FILENO, shell_pgid);
    }

    // Flush any background notifications that completed during foreground run
    flush_background_notifications();

    // If job was stopped via Ctrl-Z, track it in the active jobs list
    if (stopped) {
        char cmd_line[1024];
        format_pipeline_cmdline(pipeline, cmd_line, sizeof(cmd_line));
        TrackedJob *job = create_job(pids[0], cmd_line, false); // create stopped job
        if (job != NULL) {
            for (int i = 0; i < cmd_count; i++) {
                job_add_process(job, pids[i], pipeline->cmds[i].args[0]);
                job->procs[i].state = PROC_STOPPED; // mark stopped
            }
            printf("[%d] + Stopped    %s\n", job->job_number, job->cmd_line); // print notice
            fflush(stdout);
        }
    }

    if (pipes) free(pipes); // free pipe memory
    free(pids);             // free PID array

    return last_status; // return final exit status
}

// Executes a sequence of pipelines separated by ';' or '&'
int execute_sequence(Sequence *seq) {
    if (seq == NULL || seq->pipeline_count == 0) return 0; // empty check
    
    // Execute each pipeline in order
    for (int i = 0; i < seq->pipeline_count; i++) {
        bool failed_to_start = false; // failure to start flag
        execute_pipeline(&seq->pipelines[i], &failed_to_start);
        // Sequential execution halts if a command failed to start entirely
        if (failed_to_start) {
            break; // stop executing remaining commands
        }
    }
    return 0; // success
}
