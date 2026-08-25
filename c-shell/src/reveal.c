#include "reveal.h"
#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <fcntl.h>

static bool directory_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return false;
}

// Intrinsic: reveal
// ---------------------------------------------------------
static int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

static int list_directory(const char *dir_path, const char *prefix, bool show_hidden, bool recursive) {
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        return -1;
    }

    char **entries = NULL;
    int count = 0;
    int capacity = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!show_hidden && entry->d_name[0] == '.') {
            continue;
        }
        if (count >= capacity) {
            capacity = capacity == 0 ? 16 : capacity * 2;
            char **new_entries = realloc(entries, capacity * sizeof(char *));
            if (new_entries == NULL) {
                perror("realloc");
                closedir(dir);
                return -1;
            }
            entries = new_entries;
        }
        entries[count++] = strdup(entry->d_name);
    }
    closedir(dir);

    if (count > 0) {
        qsort(entries, count, sizeof(char *), compare_strings);
    }

    for (int i = 0; i < count; i++) {
        char full_path[MAX_PATH * 2];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entries[i]);

        struct stat st;
        bool is_dir = false;
        if (stat(full_path, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
        }

        if (is_dir) {
            printf("%s%s/\n", prefix, entries[i]);
        } else {
            printf("%s%s\n", prefix, entries[i]);
        }

        if (recursive && is_dir && strcmp(entries[i], ".") != 0 && strcmp(entries[i], "..") != 0) {
            char next_prefix[MAX_PATH * 2];
            snprintf(next_prefix, sizeof(next_prefix), "%s%s/", prefix, entries[i]);
            list_directory(full_path, next_prefix, show_hidden, recursive);
        }

        free(entries[i]);
    }
    free(entries);
    return 0;
}

int builtin_reveal(Command *cmd) {
    bool show_hidden = false;
    bool recursive = false;
    const char *path_arg = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        const char *arg = cmd->args[i];
        if (arg[0] == '-' && strlen(arg) > 1) {
            for (int j = 1; arg[j] != '\0'; j++) {
                if (arg[j] == 'a') {
                    show_hidden = true;
                } else if (arg[j] == 't') {
                    recursive = true;
                } else {
                    fprintf(stderr, "reveal: invalid syntax\n");
                    return 1;
                }
            }
        } else {
            if (path_arg != NULL) {
                fprintf(stderr, "reveal: invalid syntax\n");
                return 1;
            }
            path_arg = arg;
        }
    }

    char target_path[MAX_PATH];
    if (path_arg == NULL) {
        if (getcwd(target_path, sizeof(target_path)) == NULL) {
            perror("getcwd");
            return 1;
        }
    } else if (strcmp(path_arg, "~") == 0) {
        strncpy(target_path, shell_home_dir, sizeof(target_path) - 1);
        target_path[sizeof(target_path) - 1] = '\0';
    } else if (strcmp(path_arg, ".") == 0) {
        if (getcwd(target_path, sizeof(target_path)) == NULL) {
            perror("getcwd");
            return 1;
        }
    } else if (strcmp(path_arg, "..") == 0) {
        strncpy(target_path, "..", sizeof(target_path) - 1);
        target_path[sizeof(target_path) - 1] = '\0';
    } else if (strcmp(path_arg, "-") == 0) {
        if (previous_cwd[0] == '\0') {
            fprintf(stderr, "reveal: no such directory\n");
            return 1;
        }
        strncpy(target_path, previous_cwd, sizeof(target_path) - 1);
        target_path[sizeof(target_path) - 1] = '\0';
    } else {
        strncpy(target_path, path_arg, sizeof(target_path) - 1);
        target_path[sizeof(target_path) - 1] = '\0';
    }

    if (!directory_exists(target_path)) {
        fprintf(stderr, "reveal: no such directory\n");
        return 1;
    }

    if (list_directory(target_path, "", show_hidden, recursive) != 0) {
        fprintf(stderr, "reveal: no such directory\n");
        return 1;
    }

    return 0;
}
