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
// Helper: Checks if path exists and is a directory
static bool directory_exists(const char *path) {
    struct stat st; // stat buffer
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode); // verify directory mode
    }
    return false; // stat failed or not a directory
}

// Structure representing a recorded directory visit in frecency table
typedef struct {
    char path[MAX_PATH]; // directory path
    int count;           // visit frequency
    time_t last_time;    // last visit timestamp
} FrecencyEntry;

// Records a directory visit in .cshell_frecency
static void update_frecency(const char *path) {
    char frec_file[MAX_PATH * 2]; // frecency file path
    snprintf(frec_file, sizeof(frec_file), "%s/.cshell_frecency", shell_home_dir);

    // allocate entry buffer
    FrecencyEntry *entries = malloc(2048 * sizeof(FrecencyEntry));
    if (entries == NULL) {
        perror("malloc"); // allocation failure
        return;
    }
    int entry_count = 0; // count of loaded entries

    // Read existing entries from frecency file
    FILE *fp = fopen(frec_file, "r");
    if (fp != NULL) {
        while (entry_count < 2048) {
            char line[MAX_PATH + 100]; // line buffer
            if (fgets(line, sizeof(line), fp) == NULL) break;
            int count;
            long long last_time;
            char p[MAX_PATH];
            // parse: <count> <timestamp> <path>
            if (sscanf(line, "%d %lld %[^\n]", &count, &last_time, p) == 3) {
                entries[entry_count].count = count;                   // store count
                entries[entry_count].last_time = (time_t)last_time;   // store time
                strncpy(entries[entry_count].path, p, MAX_PATH - 1);  // store path
                entries[entry_count].path[MAX_PATH - 1] = '\0';       // null-terminate
                entry_count++;                                        // increment count
            }
        }
        fclose(fp); // close read file
    }

    // Update existing entry if path already recorded
    bool found = false;
    for (int i = 0; i < entry_count; i++) {
        if (strcmp(entries[i].path, path) == 0) {
            entries[i].count++;               // bump visit count
            entries[i].last_time = time(NULL);// update timestamp
            found = true;
            break;
        }
    }

    // Insert new entry if not already present
    if (!found && entry_count < 2048) {
        strncpy(entries[entry_count].path, path, MAX_PATH - 1);
        entries[entry_count].path[MAX_PATH - 1] = '\0';
        entries[entry_count].count = 1;                   // initial count
        entries[entry_count].last_time = time(NULL);      // current time
        entry_count++;
    }

    // Write updated entries back to disk
    fp = fopen(frec_file, "w");
    if (fp != NULL) {
        for (int i = 0; i < entry_count; i++) {
            fprintf(fp, "%d %lld %s\n", entries[i].count, (long long)entries[i].last_time, entries[i].path);
        }
        fclose(fp); // flush and close
    }
    free(entries); // release memory
}

// Finds highest ranked directory matching keyword substring
static bool frecency_lookup(const char *name, char *out_path) {
    char frec_file[MAX_PATH * 2]; // frecency file path
    snprintf(frec_file, sizeof(frec_file), "%s/.cshell_frecency", shell_home_dir);

    FILE *fp = fopen(frec_file, "r"); // open database
    if (fp == NULL) return false;      // no database found

    double best_score = -1.0;          // best frecency score
    char best_path[MAX_PATH] = "";     // matching path
    time_t now = time(NULL);           // current time for decay

    char line[MAX_PATH + 100];
    // Scan all recorded directories
    while (fgets(line, sizeof(line), fp) != NULL) {
        int count;
        long long last_time;
        char p[MAX_PATH];
        // parse record
        if (sscanf(line, "%d %lld %[^\n]", &count, &last_time, p) == 3) {
            // substring match check
            if (strstr(p, name) != NULL) {
                // verify path still exists on disk
                if (directory_exists(p)) {
                    double elapsed = difftime(now, (time_t)last_time); // elapsed seconds
                    double weight = 1.0;                               // recency weight
                    // calculate recency decay factor
                    if (elapsed < 60) weight = 1.0;
                    else if (elapsed < 3600) weight = 0.8;
                    else if (elapsed < 86400) weight = 0.6;
                    else if (elapsed < 604800) weight = 0.4;
                    else weight = 0.2;
                    double score = count * weight; // score = frequency * recency
                    // keep track of highest score
                    if (score > best_score) {
                        best_score = score;
                        strncpy(best_path, p, MAX_PATH - 1);
                        best_path[MAX_PATH - 1] = '\0';
                    }
                }
            }
        }
    }
    fclose(fp); // close file

    // Return best path if found
    if (best_score >= 0.0) {
        strncpy(out_path, best_path, MAX_PATH - 1);
        out_path[MAX_PATH - 1] = '\0';
        return true; // found
    }
    return false; // not found
}

// Builtin 'hop': changes current working directory
int builtin_hop(Command *cmd) {
    int args_to_process = cmd->argc - 1; // count of target args
    // Default: no arguments hops to shell home directory
    if (args_to_process == 0) {
        char old_cwd[MAX_PATH];
        if (getcwd(old_cwd, sizeof(old_cwd)) != NULL) {
            if (chdir(shell_home_dir) == 0) {
                strncpy(previous_cwd, old_cwd, MAX_PATH - 1); // update prev cwd
                previous_cwd[MAX_PATH - 1] = '\0';
                update_frecency(shell_home_dir);              // update frecency
                return 0;
            }
        }
        return 1; // failure
    }

    // Process each directory argument sequentially
    for (int i = 1; i < cmd->argc; i++) {
        const char *arg = cmd->args[i]; // current argument
        char target_path[MAX_PATH] = "";// resolved path

        // Resolve special arguments: '~', '.', '..', '-', or path/frecency
        if (strcmp(arg, "~") == 0) {
            strncpy(target_path, shell_home_dir, MAX_PATH - 1); // jump to home
        } else if (strcmp(arg, ".") == 0) {
            continue; // '.' is no-op, do not update frecency
        } else if (strcmp(arg, "..") == 0) {
            strncpy(target_path, "..", MAX_PATH - 1); // jump to parent
        } else if (strcmp(arg, "-") == 0) {
            // '-' jumps to previous working directory
            if (previous_cwd[0] == '\0') {
                continue; // no previous directory yet
            }
            strncpy(target_path, previous_cwd, MAX_PATH - 1);
        } else {
            // direct path or frecency lookup
            if (directory_exists(arg)) {
                strncpy(target_path, arg, MAX_PATH - 1); // direct relative/absolute path
            } else {
                // fallback to frecency lookup
                if (!frecency_lookup(arg, target_path)) {
                    fprintf(stderr, "hop: no such directory\n"); // report failure
                    return 1;                                    // halt sequence
                }
            }
        }

        target_path[MAX_PATH - 1] = '\0'; // null-terminate

        char old_cwd[MAX_PATH];
        // query current cwd before hopping
        if (getcwd(old_cwd, sizeof(old_cwd)) == NULL) {
            perror("getcwd");
            return 1;
        }

        // Perform directory change
        if (chdir(target_path) != 0) {
            perror("chdir"); // chdir error
            return 1;
        }

        // Update previous directory tracker
        strncpy(previous_cwd, old_cwd, MAX_PATH - 1);
        previous_cwd[MAX_PATH - 1] = '\0';

        // Update frecency table with newly resolved CWD
        char resolved_cwd[MAX_PATH];
        if (getcwd(resolved_cwd, sizeof(resolved_cwd)) != NULL) {
            update_frecency(resolved_cwd); // record visit
        }
    }
    return 0; // all hops completed successfully
}
