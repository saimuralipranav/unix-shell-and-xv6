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

// Helper: Checks if directory exists on disk
static bool directory_exists(const char *path) {
    struct stat st; // stat structure
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode); // true if directory
    }
    return false; // not directory or does not exist
}

// String comparison callback for ASCII lexicographical sorting
static int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b); // compare filenames
}

// Recursively or non-recursively lists directory entries
static int list_directory(const char *dir_path, const char *prefix, bool show_hidden, bool recursive) {
    DIR *dir = opendir(dir_path); // open directory stream
    if (dir == NULL) {
        return -1; // failed to open
    }

    char **entries = NULL; // array of entry names
    int count = 0;          // entry count
    int capacity = 0;       // allocated capacity

    struct dirent *entry;
    // Read all entries in the directory
    while ((entry = readdir(dir)) != NULL) {
        // always exclude '.' and '..'
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        // skip hidden files (starting with '.') unless -a is specified
        if (!show_hidden && entry->d_name[0] == '.') {
            continue;
        }
        // expand dynamic array if full
        if (count >= capacity) {
            capacity = capacity == 0 ? 16 : capacity * 2;
            char **new_entries = realloc(entries, capacity * sizeof(char *));
            if (new_entries == NULL) {
                perror("realloc"); // memory error
                closedir(dir);
                return -1;
            }
            entries = new_entries;
        }
        entries[count++] = strdup(entry->d_name); // copy entry name
    }
    closedir(dir); // close directory stream

    // Sort entries lexicographically by ASCII bare name
    if (count > 0) {
        qsort(entries, count, sizeof(char *), compare_strings);
    }

    // Process and print each sorted entry
    for (int i = 0; i < count; i++) {
        char full_path[MAX_PATH * 2]; // absolute/relative path
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entries[i]);

        struct stat st;
        bool is_dir = false; // directory flag
        if (stat(full_path, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode); // determine if directory
        }

        struct stat lst;
        bool is_lnk = false; // symlink flag
        if (lstat(full_path, &lst) == 0) {
            is_lnk = S_ISLNK(lst.st_mode); // determine if symlink
        }

        bool has_space = (strchr(entries[i], ' ') != NULL); // check for spaces
        // trailing slash strictly on directories when -t (recursive) is enabled
        const char *slash = (is_dir && recursive) ? "/" : "";

        // Wrap entry in single quotes if it contains spaces
        if (has_space) {
            printf("%s'%s'%s\n", prefix, entries[i], slash);
        } else {
            printf("%s%s%s\n", prefix, entries[i], slash);
        }

        // Recurse into subdirectories (avoid following directory symlinks)
        if (recursive && is_dir && !is_lnk && strcmp(entries[i], ".") != 0 && strcmp(entries[i], "..") != 0) {
            char next_prefix[MAX_PATH * 2];
            snprintf(next_prefix, sizeof(next_prefix), "%s%s/", prefix, entries[i]);
            list_directory(full_path, next_prefix, show_hidden, recursive); // recursive call
        }

        free(entries[i]); // free entry string
    }
    free(entries); // free array
    return 0;      // success
}

// Builtin 'reveal': lists files and subdirectories
int builtin_reveal(Command *cmd) {
    bool show_hidden = false;    // -a flag
    bool recursive = false;      // -t flag
    const char *path_arg = NULL; // directory target

    // Parse options and positional target path
    for (int i = 1; i < cmd->argc; i++) {
        const char *arg = cmd->args[i];
        // Flag argument: starts with '-' and has at least one flag char
        if (arg[0] == '-' && strlen(arg) > 1) {
            // Flags must precede target path per spec
            if (path_arg != NULL) {
                fprintf(stderr, "reveal: invalid syntax\n");
                return 1;
            }
            // Parse individual flag characters
            for (int j = 1; arg[j] != '\0'; j++) {
                if (arg[j] == 'a') {
                    show_hidden = true;  // enable hidden files
                } else if (arg[j] == 't') {
                    recursive = true;    // enable recursive listing
                } else {
                    fprintf(stderr, "reveal: invalid syntax\n"); // unrecognized flag
                    return 1;
                }
            }
        } else {
            // Target path argument
            if (path_arg != NULL) {
                fprintf(stderr, "reveal: invalid syntax\n"); // too many path arguments
                return 1;
            }
            path_arg = arg; // store target
        }
    }

    char target_path[MAX_PATH]; // resolved directory path
    // Resolve path argument: default CWD, '~', '.', '..', '-', or literal path
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
        // check if previous directory exists
        if (previous_cwd[0] == '\0') {
            fprintf(stderr, "reveal: no such directory\n"); // no previous cwd
            return 1;
        }
        strncpy(target_path, previous_cwd, sizeof(target_path) - 1);
        target_path[sizeof(target_path) - 1] = '\0';
    } else {
        strncpy(target_path, path_arg, sizeof(target_path) - 1);
        target_path[sizeof(target_path) - 1] = '\0';
    }

    // Verify directory exists before listing
    if (!directory_exists(target_path)) {
        fprintf(stderr, "reveal: no such directory\n"); // directory not found
        return 1;
    }

    // Execute directory listing
    if (list_directory(target_path, "", show_hidden, recursive) != 0) {
        fprintf(stderr, "reveal: no such directory\n"); // list error
        return 1;
    }

    return 0; // success
}
