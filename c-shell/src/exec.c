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
static bool is_executable(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISREG(st.st_mode) && (access(path, X_OK) == 0)) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------
// Resolve command path according to C1 requirements
// ---------------------------------------------------------
static bool resolve_command(const char *cmd_name, char *out_path, size_t max_len) {
    if (cmd_name == NULL || strlen(cmd_name) == 0) return false;

    // Rule 1: contains a '/' -> treat as literal path
    if (strchr(cmd_name, '/') != NULL) {
        strncpy(out_path, cmd_name, max_len - 1);
        out_path[max_len - 1] = '\0';
        return is_executable(out_path);
    }

    // Rule 3: starts with '%' -> skip CWD, search PATH
    if (cmd_name[0] == '%') {
        const char *real_name = cmd_name + 1;
        const char *path_env = getenv("PATH");
        if (path_env != NULL) {
            char *path_copy = strdup(path_env);
            if (path_copy != NULL) {
                char *dir = strtok(path_copy, ":");
                while (dir != NULL) {
                    char full_path[MAX_PATH];
                    if (strlen(dir) == 0) {
                        snprintf(full_path, sizeof(full_path), "./%s", real_name);
                    } else {
                        snprintf(full_path, sizeof(full_path), "%s/%s", dir, real_name);
                    }
                    if (is_executable(full_path)) {
                        strncpy(out_path, full_path, max_len - 1);
                        out_path[max_len - 1] = '\0';
                        free(path_copy);
                        return true;
                    }
                    dir = strtok(NULL, ":");
                }
                free(path_copy);
            }
        }
        return false;
    }

    // Rule 2: check CWD first, then search PATH
    char cwd_path[MAX_PATH];
    snprintf(cwd_path, sizeof(cwd_path), "./%s", cmd_name);
    if (is_executable(cwd_path)) {
        strncpy(out_path, cwd_path, max_len - 1);
        out_path[max_len - 1] = '\0';
        return true;
    }

    // Search PATH
    const char *path_env = getenv("PATH");
    if (path_env != NULL) {
        char *path_copy = strdup(path_env);
        if (path_copy != NULL) {
            char *dir = strtok(path_copy, ":");
            while (dir != NULL) {
                char full_path[MAX_PATH];
                if (strlen(dir) == 0) {
                    snprintf(full_path, sizeof(full_path), "./%s", cmd_name);
                } else {
                    snprintf(full_path, sizeof(full_path), "%s/%s", dir, cmd_name);
                }
                if (is_executable(full_path)) {
                    strncpy(out_path, full_path, max_len - 1);
                    out_path[max_len - 1] = '\0';
                    free(path_copy);
                    return true;
                }
                dir = strtok(NULL, ":");
            }
            free(path_copy);
        }
    }

    return false;
}

// ---------------------------------------------------------
// Redirection Verification
// ---------------------------------------------------------
static bool verify_input_files(Command *cmd) {
    for (int i = 0; i < cmd->in_redir_count; i++) {
        int fd = open(cmd->in_redirs[i].filepath, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "cshell: no such file or directory\n");
            return false;
        }
        close(fd);
    }
    return true;
}

static bool verify_output_files(Command *cmd) {
    for (int i = 0; i < cmd->out_redir_count; i++) {
        int flags = O_WRONLY | O_CREAT | (cmd->out_redirs[i].append ? O_APPEND : O_TRUNC);
        int fd = open(cmd->out_redirs[i].filepath, flags, 0644);
        if (fd < 0) {
            fprintf(stderr, "cshell: unable to create file for writing\n");
            return false;
        }
        close(fd);
    }
    return true;
}

// ---------------------------------------------------------
// Helper Process Functions for Multi-Redirection
// ---------------------------------------------------------
static void run_input_helper(Command *cmd, int write_fd) {
    for (int i = 0; i < cmd->in_redir_count; i++) {
        int fd = open(cmd->in_redirs[i].filepath, O_RDONLY);
        if (fd >= 0) {
            char buf[4096];
            ssize_t r;
            while ((r = read(fd, buf, sizeof(buf))) > 0) {
                ssize_t written = 0;
                while (written < r) {
                    ssize_t w = write(write_fd, buf + written, r - written);
                    if (w < 0) break;
                    written += w;
                }
            }
            close(fd);
        }
    }
}

static void run_output_helper(Command *cmd, int read_fd) {
    int *fds = malloc(cmd->out_redir_count * sizeof(int));
    if (fds == NULL) {
        perror("malloc");
        return;
    }
    for (int i = 0; i < cmd->out_redir_count; i++) {
        int flags = O_WRONLY | O_CREAT | (cmd->out_redirs[i].append ? O_APPEND : O_TRUNC);
        fds[i] = open(cmd->out_redirs[i].filepath, flags, 0644);
    }

    char buf[4096];
    ssize_t r;
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

    for (int i = 0; i < cmd->out_redir_count; i++) {
        if (fds[i] >= 0) {
            close(fds[i]);
        }
    }
    free(fds);
}

// ---------------------------------------------------------
// Job Control and Command Formatting
// ---------------------------------------------------------
static void format_pipeline_cmdline(Pipeline *pipeline, char *out, size_t max_len) {
    out[0] = '\0';
    for (int i = 0; i < pipeline->cmd_count; i++) {
        if (i > 0) {
            strncat(out, " | ", max_len - strlen(out) - 1);
        }
        for (int a = 0; a < pipeline->cmds[i].argc; a++) {
            if (a > 0) strncat(out, " ", max_len - strlen(out) - 1);
            strncat(out, pipeline->cmds[i].args[a], max_len - strlen(out) - 1);
        }
    }
}

void check_background_jobs(void) {
    reap_background_jobs();
}

// ---------------------------------------------------------
// Pipeline Execution
// ---------------------------------------------------------
int execute_pipeline(Pipeline *pipeline, bool *failed_to_start) {
    if (pipeline == NULL || pipeline->cmd_count == 0) return 0;
    if (failed_to_start) *failed_to_start = false;

    int cmd_count = pipeline->cmd_count;

    for (int i = 0; i < cmd_count; i++) {
        if (!verify_input_files(&pipeline->cmds[i]) || !verify_output_files(&pipeline->cmds[i])) {
            return 1;
        }
    }

    if (cmd_count == 1) {
        Command *cmd = &pipeline->cmds[0];
        char resolved_path[MAX_PATH];
        
        bool is_b = is_builtin(cmd->args[0]);
        if (!is_b && !resolve_command(cmd->args[0], resolved_path, sizeof(resolved_path))) {
            fprintf(stderr, "cshell: command not found (%s)\n", cmd->args[0]);
            if (failed_to_start) *failed_to_start = true;
            return -2;
        }

        if (is_b && !pipeline->is_background) {
            int saved_in = dup(STDIN_FILENO);
            int saved_out = dup(STDOUT_FILENO);

            pid_t helper_in = -1;
            pid_t helper_out = -1;

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

            int status = exec_builtin(cmd);

            dup2(saved_in, STDIN_FILENO); close(saved_in);
            dup2(saved_out, STDOUT_FILENO); close(saved_out);

            if (helper_in > 0) waitpid(helper_in, NULL, 0);
            if (helper_out > 0) waitpid(helper_out, NULL, 0);
            return status;
        }
    }

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

    pid_t *pids = malloc(cmd_count * sizeof(pid_t));
    if (pids == NULL) {
        perror("malloc");
        if (pipes) free(pipes);
        return 1;
    }

    for (int i = 0; i < cmd_count; i++) {
        Command *cmd = &pipeline->cmds[i];
        pids[i] = fork();

        if (pids[i] == 0) {
            // Child: set process group
            setpgid(0, (i == 0 ? 0 : pids[0]));

            // Reset signal dispositions for child
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);

            if (i > 0) dup2(pipes[i - 1][0], STDIN_FILENO);
            if (i < cmd_count - 1) dup2(pipes[i][1], STDOUT_FILENO);

            for (int p = 0; p < cmd_count - 1; p++) {
                close(pipes[p][0]); close(pipes[p][1]);
            }

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

            if (is_builtin(cmd->args[0])) {
                exit(exec_builtin(cmd));
            } else {
                char resolved_path[MAX_PATH];
                if (resolve_command(cmd->args[0], resolved_path, sizeof(resolved_path))) {
                    if (cmd->args[0][0] == '%') cmd->args[0]++;
                    execv(resolved_path, cmd->args);
                    perror("execv");
                    exit(1);
                } else {
                    fprintf(stderr, "cshell: command not found (%s)\n", cmd->args[0]);
                    exit(127);
                }
            }
        } else if (pids[i] > 0) {
            // Parent: set process group immediately after fork
            setpgid(pids[i], (i == 0 ? pids[0] : pids[0]));
        }
    }

    for (int p = 0; p < cmd_count - 1; p++) {
        close(pipes[p][0]); close(pipes[p][1]);
    }

    if (pipeline->is_background) {
        char cmd_line[1024];
        format_pipeline_cmdline(pipeline, cmd_line, sizeof(cmd_line));
        TrackedJob *job = create_job(pids[0], cmd_line, true);
        if (job != NULL) {
            for (int i = 0; i < cmd_count; i++) {
                job_add_process(job, pids[i], pipeline->cmds[i].args[0]);
            }
            printf("[%d] %d\n", job->job_number, pids[0]);
            fflush(stdout);
        }
        if (pipes) free(pipes);
        free(pids);
        return 0; // Return immediately for background jobs
    }

    // Foreground execution: give terminal to the child process group
    pid_t shell_pgid = getpgrp();
    if (isatty(STDIN_FILENO)) {
        tcsetpgrp(STDIN_FILENO, pids[0]);
    }

    int last_status = 0;
    bool stopped = false;
    for (int i = 0; i < cmd_count; i++) {
        int status;
        if (waitpid(pids[i], &status, WUNTRACED) == pids[i]) {
            if (WIFSTOPPED(status)) {
                stopped = true;
            } else if (i == cmd_count - 1) {
                if (WIFEXITED(status)) last_status = WEXITSTATUS(status);
                else if (WIFSIGNALED(status)) last_status = 128 + WTERMSIG(status);
            }
        }
    }

    // Reclaim terminal for shell
    if (isatty(STDIN_FILENO)) {
        tcsetpgrp(STDIN_FILENO, shell_pgid);
    }

    if (stopped) {
        char cmd_line[1024];
        format_pipeline_cmdline(pipeline, cmd_line, sizeof(cmd_line));
        TrackedJob *job = create_job(pids[0], cmd_line, false);
        if (job != NULL) {
            for (int i = 0; i < cmd_count; i++) {
                job_add_process(job, pids[i], pipeline->cmds[i].args[0]);
                job->procs[i].state = PROC_STOPPED;
            }
            printf("[%d] + Stopped    %s\n", job->job_number, job->cmd_line);
            fflush(stdout);
        }
    }

    if (pipes) free(pipes);
    free(pids);

    return last_status;
}

int execute_sequence(Sequence *seq) {
    if (seq == NULL || seq->pipeline_count == 0) return 0;
    
    for (int i = 0; i < seq->pipeline_count; i++) {
        bool failed_to_start = false;
        execute_pipeline(&seq->pipelines[i], &failed_to_start);
        if (failed_to_start) {
            break;
        }
    }
    return 0;
}
