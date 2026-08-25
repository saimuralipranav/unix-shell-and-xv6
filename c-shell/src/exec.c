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
            
            if (cmd->out_redir_count > 0) {
                int flags = O_WRONLY | O_CREAT | (cmd->out_redirs[0].append ? O_APPEND : O_TRUNC);
                int fd = open(cmd->out_redirs[0].filepath, flags, 0644);
                if (fd >= 0) {
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
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
            
            if (cmd->out_redir_count > 0) {
                int flags = O_WRONLY | O_CREAT | (cmd->out_redirs[0].append ? O_APPEND : O_TRUNC);
                int fd = open(cmd->out_redirs[0].filepath, flags, 0644);
                if (fd >= 0) {
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
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
    
    printf("Pipelines not implemented in this commit.\n");
    return 1;
}
