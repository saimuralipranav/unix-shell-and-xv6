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
    
    // 1. Resolve path with '/' directly
    if (strchr(cmd_name, '/') != NULL) {
        strncpy(out_path, cmd_name, max_len - 1);
        out_path[max_len - 1] = '\0';
        return is_executable(out_path);
    }
    
    // 2. '%' prefix bypasses CWD, searches PATH directly
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
    
    // 3. Resolve name without '/' in CWD first
    char cwd_path[MAX_PATH];
    snprintf(cwd_path, sizeof(cwd_path), "./%s", cmd_name);
    if (is_executable(cwd_path)) {
        strncpy(out_path, cwd_path, max_len - 1);
        out_path[max_len - 1] = '\0';
        return true;
    }
    
    // 4. Search PATH directories in order
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

int execute_pipeline(Pipeline *pipeline) {
    if (pipeline == NULL || pipeline->cmd_count == 0) return 0;
    
    // At this stage, we only handle single command execution (no redirects, no pipes)
    if (pipeline->cmd_count == 1) {
        Command *cmd = &pipeline->cmds[0];
        
        if (is_builtin(cmd->args[0])) {
            return exec_builtin(cmd);
        }
        
        pid_t pid = fork();
        if (pid == 0) {
            // Child process
            char resolved_path[MAX_PATH];
            if (resolve_command(cmd->args[0], resolved_path, sizeof(resolved_path))) {
                // If prefixed with %, skip it for execv argv[0]
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
            // Parent process
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
    
    printf("Pipelines and redirection not implemented in this commit.\n");
    return 1;
}
