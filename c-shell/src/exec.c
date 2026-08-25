#include "exec.h"
#include "builtins.h"
#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>

static bool is_executable(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISREG(st.st_mode) && (access(path, X_OK) == 0)) {
            return true;
        }
    }
    return false;
}

static bool resolve_command(const char *cmd_name, char *out_path, size_t max_len) {
    if (cmd_name == NULL || strlen(cmd_name) == 0) return false;
    
    if (strchr(cmd_name, '/') != NULL) {
        strncpy(out_path, cmd_name, max_len - 1);
        out_path[max_len - 1] = '\0';
        return is_executable(out_path);
    }
    
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
    
    char cwd_path[MAX_PATH];
    snprintf(cwd_path, sizeof(cwd_path), "./%s", cmd_name);
    if (is_executable(cwd_path)) {
        strncpy(out_path, cwd_path, max_len - 1);
        out_path[max_len - 1] = '\0';
        return true;
    }
    
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

int execute_pipeline(Pipeline *pipeline) {
    if (pipeline == NULL || pipeline->cmd_count == 0) return 0;
    
    if (pipeline->cmd_count == 1) {
        Command *cmd = &pipeline->cmds[0];
        
        if (!verify_input_files(cmd) || !verify_output_files(cmd)) {
            return 1;
        }
        
        if (is_builtin(cmd->args[0])) {
            int saved_in = dup(STDIN_FILENO);
            int saved_out = dup(STDOUT_FILENO);
            
            if (cmd->in_redir_count == 1) {
                int fd = open(cmd->in_redirs[0].filepath, O_RDONLY);
                if (fd >= 0) {
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }
            } else if (cmd->in_redir_count > 1) {
                int in_p[2];
                if (pipe(in_p) == 0) {
                    pid_t helper = fork();
                    if (helper == 0) {
                        close(in_p[0]);
                        run_input_helper(cmd, in_p[1]);
                        close(in_p[1]);
                        exit(0);
                    }
                    close(in_p[1]);
                    dup2(in_p[0], STDIN_FILENO);
                    close(in_p[0]);
                }
            }
            
            if (cmd->out_redir_count == 1) {
                int flags = O_WRONLY | O_CREAT | (cmd->out_redirs[0].append ? O_APPEND : O_TRUNC);
                int fd = open(cmd->out_redirs[0].filepath, flags, 0644);
                if (fd >= 0) {
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
            } else if (cmd->out_redir_count > 1) {
                int out_p[2];
                if (pipe(out_p) == 0) {
                    pid_t helper = fork();
                    if (helper == 0) {
                        close(out_p[1]);
                        run_output_helper(cmd, out_p[0]);
                        close(out_p[0]);
                        exit(0);
                    }
                    close(out_p[0]);
                    dup2(out_p[1], STDOUT_FILENO);
                    close(out_p[1]);
                }
            }
            
            int status = exec_builtin(cmd);
            
            dup2(saved_in, STDIN_FILENO);
            close(saved_in);
            dup2(saved_out, STDOUT_FILENO);
            close(saved_out);
            
            return status;
        }
        
        pid_t pid = fork();
        if (pid == 0) {
            if (cmd->in_redir_count == 1) {
                int fd = open(cmd->in_redirs[0].filepath, O_RDONLY);
                if (fd >= 0) {
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }
            } else if (cmd->in_redir_count > 1) {
                int in_p[2];
                if (pipe(in_p) == 0) {
                    pid_t helper = fork();
                    if (helper == 0) {
                        close(in_p[0]);
                        run_input_helper(cmd, in_p[1]);
                        close(in_p[1]);
                        exit(0);
                    }
                    close(in_p[1]);
                    dup2(in_p[0], STDIN_FILENO);
                    close(in_p[0]);
                }
            }
            
            if (cmd->out_redir_count == 1) {
                int flags = O_WRONLY | O_CREAT | (cmd->out_redirs[0].append ? O_APPEND : O_TRUNC);
                int fd = open(cmd->out_redirs[0].filepath, flags, 0644);
                if (fd >= 0) {
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
            } else if (cmd->out_redir_count > 1) {
                int out_p[2];
                if (pipe(out_p) == 0) {
                    pid_t helper = fork();
                    if (helper == 0) {
                        close(out_p[1]);
                        run_output_helper(cmd, out_p[0]);
                        close(out_p[0]);
                        exit(0);
                    }
                    close(out_p[0]);
                    dup2(out_p[1], STDOUT_FILENO);
                    close(out_p[1]);
                }
            }
            
            char resolved_path[MAX_PATH];
            if (resolve_command(cmd->args[0], resolved_path, sizeof(resolved_path))) {
                if (cmd->args[0][0] == '%') {
                    cmd->args[0] = cmd->args[0] + 1;
                }
                execv(resolved_path, cmd->args);
                perror("execv");
                exit(EXIT_FAILURE);
            } else {
                fprintf(stderr, "cshell: command not found (%s)\n", cmd->args[0]);
                exit(127);
            }
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status)) {
                return WEXITSTATUS(status);
            }
            return 1;
        } else {
            perror("fork");
            return 1;
        }
    }
    
    int cmd_count = pipeline->cmd_count;

    // Verify all redirections for all commands first
    for (int i = 0; i < cmd_count; i++) {
        if (!verify_input_files(&pipeline->cmds[i]) || !verify_output_files(&pipeline->cmds[i])) {
            return 1;
        }
    }

    // Allocate pipes
    int (*pipes)[2] = malloc((cmd_count - 1) * sizeof(*pipes));
    if (pipes == NULL) {
        perror("malloc");
        return 1;
    }
    for (int i = 0; i < cmd_count - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            free(pipes);
            return 1;
        }
    }

    // Spawn child processes
    pid_t *pids = malloc(cmd_count * sizeof(pid_t));
    if (pids == NULL) {
        perror("malloc");
        free(pipes);
        return 1;
    }

    for (int i = 0; i < cmd_count; i++) {
        Command *cmd = &pipeline->cmds[i];
        pids[i] = fork();

        if (pids[i] == 0) {
            // Child process
            // Set up pipeline input redirection
            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }
            // Set up pipeline output redirection
            if (i < cmd_count - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            // Close all pipeline pipe descriptors in the child
            for (int p = 0; p < cmd_count - 1; p++) {
                close(pipes[p][0]);
                close(pipes[p][1]);
            }



            // Execute the command
            if (is_builtin(cmd->args[0])) {
                int status = exec_builtin(cmd);
                exit(status);
            } else {
                char resolved_path[MAX_PATH];
                if (resolve_command(cmd->args[0], resolved_path, sizeof(resolved_path))) {
                    char *original_argv0 = cmd->args[0];
                    if (cmd->args[0][0] == '%') {
                        cmd->args[0] = cmd->args[0] + 1;
                    }
                    execv(resolved_path, cmd->args);
                    perror("execv");
                    cmd->args[0] = original_argv0; // Keep state clean if execution failed
                    exit(1);
                } else {
                    fprintf(stderr, "cshell: command not found (%s)\n", cmd->args[0]);
                    exit(127);
                }
            }
        } else if (pids[i] < 0) {
            perror("fork");
            for (int p = 0; p < cmd_count - 1; p++) {
                close(pipes[p][0]);
                close(pipes[p][1]);
            }
            free(pipes);
            free(pids);
            return 1;
        }
    }

    // Parent process cleanup and wait
    for (int p = 0; p < cmd_count - 1; p++) {
        close(pipes[p][0]);
        close(pipes[p][1]);
    }

    int last_status = 0;
    for (int i = 0; i < cmd_count; i++) {
        int status;
        if (waitpid(pids[i], &status, 0) == pids[i]) {
            if (i == cmd_count - 1) {
                if (WIFEXITED(status)) {
                    last_status = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    last_status = 128 + WTERMSIG(status);
                }
            }
        }
    }

    // Reap all helper/tee processes
    while (wait(NULL) > 0);

    free(pipes);
    free(pids);

    return last_status;
}
