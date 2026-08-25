#include "hop.h"
#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <fcntl.h>

// ---------------------------------------------------------
// Helper: Check if directory exists
// ---------------------------------------------------------
static bool directory_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return false;
}

// ---------------------------------------------------------
// Helper: Check if file is executable and not a directory
// ---------------------------------------------------------


// ---------------------------------------------------------
// Frecency Scoring and Persistence for "hop"
// ---------------------------------------------------------
typedef struct {
    char path[MAX_PATH];
    int count;
    time_t last_time;
} FrecencyEntry;

static void update_frecency(const char *path) {
    char frec_file[MAX_PATH * 2];
    snprintf(frec_file, sizeof(frec_file), "%s/.cshell_frecency", shell_home_dir);

    FrecencyEntry *entries = malloc(2048 * sizeof(FrecencyEntry));
    if (entries == NULL) {
        perror("malloc");
        return;
    }
    int entry_count = 0;

    FILE *fp = fopen(frec_file, "r");
    if (fp != NULL) {
        while (entry_count < 2048) {
            char line[MAX_PATH + 100];
            if (fgets(line, sizeof(line), fp) == NULL) break;
            int count;
            long long last_time;
            char p[MAX_PATH];
            if (sscanf(line, "%d %lld %[^\n]", &count, &last_time, p) == 3) {
                entries[entry_count].count = count;
                entries[entry_count].last_time = (time_t)last_time;
                strncpy(entries[entry_count].path, p, MAX_PATH - 1);
                entries[entry_count].path[MAX_PATH - 1] = '\0';
                entry_count++;
            }
        }
        fclose(fp);
    }

    bool found = false;
    for (int i = 0; i < entry_count; i++) {
        if (strcmp(entries[i].path, path) == 0) {
            entries[i].count++;
            entries[i].last_time = time(NULL);
            found = true;
            break;
        }
    }

    if (!found && entry_count < 2048) {
        strncpy(entries[entry_count].path, path, MAX_PATH - 1);
        entries[entry_count].path[MAX_PATH - 1] = '\0';
        entries[entry_count].count = 1;
        entries[entry_count].last_time = time(NULL);
        entry_count++;
    }

    fp = fopen(frec_file, "w");
    if (fp != NULL) {
        for (int i = 0; i < entry_count; i++) {
            fprintf(fp, "%d %lld %s\n", entries[i].count, (long long)entries[i].last_time, entries[i].path);
        }
        fclose(fp);
    }
    free(entries);
}

static bool frecency_lookup(const char *name, char *out_path) {
    char frec_file[MAX_PATH * 2];
    snprintf(frec_file, sizeof(frec_file), "%s/.cshell_frecency", shell_home_dir);

    FILE *fp = fopen(frec_file, "r");
    if (fp == NULL) return false;

    double best_score = -1.0;
    char best_path[MAX_PATH] = "";
    time_t now = time(NULL);

    char line[MAX_PATH + 100];
    while (fgets(line, sizeof(line), fp) != NULL) {
        int count;
        long long last_time;
        char p[MAX_PATH];
        if (sscanf(line, "%d %lld %[^\n]", &count, &last_time, p) == 3) {
            if (strstr(p, name) != NULL) {
                if (directory_exists(p)) {
                    double elapsed = difftime(now, (time_t)last_time);
                    double weight = 1.0;
                    if (elapsed < 60) weight = 1.0;
                    else if (elapsed < 3600) weight = 0.8;
                    else if (elapsed < 86400) weight = 0.6;
                    else if (elapsed < 604800) weight = 0.4;
                    else weight = 0.2;
                    double score = count * weight;
                    if (score > best_score) {
                        best_score = score;
                        strncpy(best_path, p, MAX_PATH - 1);
                        best_path[MAX_PATH - 1] = '\0';
                    }
                }
            }
        }
    }
    fclose(fp);

    if (best_score >= 0.0) {
        strncpy(out_path, best_path, MAX_PATH - 1);
        out_path[MAX_PATH - 1] = '\0';
        return true;
    }
    return false;
}

// ---------------------------------------------------------
// Intrinsic: hop
// ---------------------------------------------------------
int builtin_hop(Command *cmd) {
    int args_to_process = cmd->argc - 1;
    if (args_to_process == 0) {
        char old_cwd[MAX_PATH];
        if (getcwd(old_cwd, sizeof(old_cwd)) != NULL) {
            if (chdir(shell_home_dir) == 0) {
                strncpy(previous_cwd, old_cwd, MAX_PATH - 1);
                previous_cwd[MAX_PATH - 1] = '\0';
                update_frecency(shell_home_dir);
                return 0;
            }
        }
        return 1;
    }

    for (int i = 1; i < cmd->argc; i++) {
        const char *arg = cmd->args[i];
        char target_path[MAX_PATH] = "";

        if (strcmp(arg, "~") == 0) {
            strncpy(target_path, shell_home_dir, MAX_PATH - 1);
        } else if (strcmp(arg, ".") == 0) {
            continue;
        } else if (strcmp(arg, "..") == 0) {
            strncpy(target_path, "..", MAX_PATH - 1);
        } else if (strcmp(arg, "-") == 0) {
            if (previous_cwd[0] == '\0') {
                continue;
            }
            strncpy(target_path, previous_cwd, MAX_PATH - 1);
        } else {
            if (directory_exists(arg)) {
                strncpy(target_path, arg, MAX_PATH - 1);
            } else {
                if (!frecency_lookup(arg, target_path)) {
                    fprintf(stderr, "hop: no such directory\n");
                    return 1;
                }
            }
        }

        target_path[MAX_PATH - 1] = '\0';

        char old_cwd[MAX_PATH];
        if (getcwd(old_cwd, sizeof(old_cwd)) == NULL) {
            perror("getcwd");
            return 1;
        }

        if (chdir(target_path) != 0) {
            perror("chdir");
            return 1;
        }

        strncpy(previous_cwd, old_cwd, MAX_PATH - 1);
        previous_cwd[MAX_PATH - 1] = '\0';

        char resolved_cwd[MAX_PATH];
        if (getcwd(resolved_cwd, sizeof(resolved_cwd)) != NULL) {
            update_frecency(resolved_cwd);
        }
    }
    return 0;
}
