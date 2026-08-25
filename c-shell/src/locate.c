#include "locate.h"
#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

static bool is_executable(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISREG(st.st_mode) && (access(path, X_OK) == 0)) {
            return true;
        }
    }
    return false;
}

int builtin_locate(Command *cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "locate: invalid syntax\n");
        return 1;
    }

    int exit_status = 0;

    for (int i = 1; i < cmd->argc; i++) {
        const char *filename = cmd->args[i];
        bool found_any = false;

        // 1. Check CWD
        char cwd[MAX_PATH];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            char full_path[MAX_PATH * 2];
            snprintf(full_path, sizeof(full_path), "%s/%s", cwd, filename);
            if (is_executable(full_path)) {
                char resolved[MAX_PATH];
                if (realpath(full_path, resolved) != NULL) {
                    printf("%s\n", resolved);
                    found_any = true;
                }
            }
        }

        // 2. Check PATH
        const char *path_env = getenv("PATH");
        if (path_env != NULL) {
            char *path_copy = strdup(path_env);
            if (path_copy != NULL) {
                char *dir = strtok(path_copy, ":");
                while (dir != NULL) {
                    char full_path[MAX_PATH * 3];
                    if (strlen(dir) == 0) {
                        snprintf(full_path, sizeof(full_path), "%s/%s", cwd, filename);
                    } else if (dir[0] == '/') {
                        snprintf(full_path, sizeof(full_path), "%s/%s", dir, filename);
                    } else {
                        snprintf(full_path, sizeof(full_path), "%s/%s/%s", cwd, dir, filename);
                    }

                    if (is_executable(full_path)) {
                        char resolved[MAX_PATH];
                        if (realpath(full_path, resolved) != NULL) {
                            printf("%s\n", resolved);
                            found_any = true;
                        }
                    }
                    dir = strtok(NULL, ":");
                }
                free(path_copy);
            }
        }

        if (!found_any) {
            fprintf(stderr, "locate: command not found (%s)\n", filename);
            exit_status = 1;
        }
    }

    return exit_status;
}
