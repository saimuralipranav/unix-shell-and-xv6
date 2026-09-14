#include "locate.h"
#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

// Checks if a path points to a regular file with execution permissions
static bool is_executable(const char *path) {
    struct stat st; // stat structure
    // query file status
    if (stat(path, &st) == 0) {
        // verify it is a regular file and executable by current user
        if (S_ISREG(st.st_mode) && (access(path, X_OK) == 0)) {
            return true; // executable file
        }
    }
    return false; // not executable or does not exist
}

// Builtin 'locate': searches CWD then PATH for matching executables
int builtin_locate(Command *cmd) {
    // Check for minimum arguments
    if (cmd->argc < 2) {
        fprintf(stderr, "locate: invalid syntax\n"); // error when no command given
        return 1;
    }

    int exit_status = 0; // overall exit code

    // Iterate through all specified target command names
    for (int i = 1; i < cmd->argc; i++) {
        const char *filename = cmd->args[i]; // target executable name
        bool found_any = false;              // match flag for this argument

        // 1. Check current working directory first
        char cwd[MAX_PATH];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            char full_path[MAX_PATH * 2]; // full path buffer
            snprintf(full_path, sizeof(full_path), "%s/%s", cwd, filename); // construct path
            // check if executable exists in cwd
            if (is_executable(full_path)) {
                printf("%s\n", full_path); // print absolute path
                found_any = true;          // mark found
            }
        }

        // 2. Search each directory in PATH in order
        const char *path_env = getenv("PATH"); // query PATH environment variable
        if (path_env != NULL) {
            char *path_copy = strdup(path_env); // duplicate PATH string for tokenization
            if (path_copy != NULL) {
                char *dir = strtok(path_copy, ":"); // tokenize on colon
                while (dir != NULL) {
                    char full_path[MAX_PATH * 3]; // path buffer
                    size_t dlen = strlen(dir);    // length of directory string
                    // handle empty directory entry as cwd
                    if (dlen == 0) {
                        snprintf(full_path, sizeof(full_path), "%s/%s", cwd, filename);
                    } else if (dir[0] == '/') { // absolute PATH entry
                        if (dir[dlen - 1] == '/') {
                            snprintf(full_path, sizeof(full_path), "%s%s", dir, filename);
                        } else {
                            snprintf(full_path, sizeof(full_path), "%s/%s", dir, filename);
                        }
                    } else { // relative PATH entry
                        snprintf(full_path, sizeof(full_path), "%s/%s/%s", cwd, dir, filename);
                    }

                    // check if executable exists at this PATH location
                    if (is_executable(full_path)) {
                        printf("%s\n", full_path); // print path without symlink resolution
                        found_any = true;          // mark found
                    }
                    dir = strtok(NULL, ":"); // next PATH entry
                }
                free(path_copy); // free duplicated string
            }
        }

        // Report command not found if no match was found in CWD or PATH
        if (!found_any) {
            fprintf(stderr, "locate: command not found (%s)\n", filename);
            exit_status = 1; // set error exit status
        }
    }

    return exit_status; // return final status
}
