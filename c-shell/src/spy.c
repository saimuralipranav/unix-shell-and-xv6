#include "spy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>

// Map st_mode or pseudo-path to display string
static const char *get_file_type_str(mode_t mode, const char *path) {
    if (S_ISDIR(mode)) return "DIR";     // Directory
    if (S_ISREG(mode)) return "REG";     // Regular file
    if (S_ISCHR(mode)) return "CHR";     // Character device
    if (S_ISBLK(mode)) return "BLK";     // Block device
    if (S_ISFIFO(mode)) return "FIFO";   // Named pipe
    if (S_ISSOCK(mode)) return "SOCK";   // Socket
    if (S_ISLNK(mode)) return "LNK";     // Symbolic link
    if (path != NULL && strncmp(path, "pipe:", 5) == 0) return "FIFO";    // Linux pipe
    if (path != NULL && strncmp(path, "socket:", 7) == 0) return "SOCK"; // Linux socket
    return "REG"; // Default fallback
}

// Comparison helper for sorting numeric file descriptors ascending
static int compare_ints(const void *a, const void *b) {
    return (*(const int *)a - *(const int *)b);
}

// Builtin command: spy [PID]
int builtin_spy(Command *cmd) {
    if (cmd == NULL) return 1; // Validate argument

    // Max 1 argument allowed
    if (cmd->argc > 2) {
        fprintf(stderr, "spy: invalid syntax\n");
        return 1;
    }

    // Default target pid is self
    pid_t pid = getpid();
    if (cmd->argc == 2) {
        // Validate pid string format
        const char *arg = cmd->args[1];
        for (int i = 0; arg[i] != '\0'; i++) {
            if (!isdigit((unsigned char)arg[i])) { // Check if all digits
                fprintf(stderr, "spy: invalid syntax\n");
                return 1;
            }
        }
        pid = (pid_t)atoi(arg); // Convert to pid_t
        if (pid <= 0) { // PID must be positive
            fprintf(stderr, "spy: no such process\n");
            return 1;
        }

        // Check if process exists and permissions
        if (kill(pid, 0) == -1) {
            if (errno == EPERM) { // Exists but unprivileged
                fprintf(stderr, "spy: permission denied\n");
                return 1;
            } else { // Does not exist
                fprintf(stderr, "spy: no such process\n");
                return 1;
            }
        }
    }

    // Check /proc/<pid> existence
    char proc_dir[64];
    snprintf(proc_dir, sizeof(proc_dir), "/proc/%d", pid);
    struct stat proc_st;
    if (stat(proc_dir, &proc_st) != 0 || !S_ISDIR(proc_st.st_mode)) {
        if (errno == EACCES || errno == EPERM) { // Access forbidden
            fprintf(stderr, "spy: permission denied\n");
        } else { // Not found
            fprintf(stderr, "spy: no such process\n");
        }
        return 1;
    }

    // Print table header
    printf("%-6s %-6s %-6s %s\n", "PID", "FD", "TYPE", "PATH");

    // 1. Inspect cwd symlink
    char cwd_link[64];
    snprintf(cwd_link, sizeof(cwd_link), "/proc/%d/cwd", pid);
    char cwd_path[1024];
    ssize_t len = readlink(cwd_link, cwd_path, sizeof(cwd_path) - 1);
    if (len > 0) { // Resolve cwd
        cwd_path[len] = '\0';
        struct stat st;
        const char *t_str = "DIR";
        if (stat(cwd_link, &st) == 0) { // Stat target
            t_str = get_file_type_str(st.st_mode, cwd_path);
        }
        printf("%-6d %-6s %-6s %s\n", pid, "cwd", t_str, cwd_path); // Display cwd row
    }

    // 2. Inspect txt (executable) symlink
    char exe_link[64];
    snprintf(exe_link, sizeof(exe_link), "/proc/%d/exe", pid);
    char exe_path[1024];
    exe_path[0] = '\0';
    len = readlink(exe_link, exe_path, sizeof(exe_path) - 1);
    if (len > 0) { // Resolve executable
        exe_path[len] = '\0';
        struct stat st;
        const char *t_str = "REG";
        if (stat(exe_link, &st) == 0) { // Stat executable
            t_str = get_file_type_str(st.st_mode, exe_path);
        }
        printf("%-6d %-6s %-6s %s\n", pid, "txt", t_str, exe_path); // Display txt row
    }

    // 3. Inspect mem (memory mapped files from /proc/<pid>/maps)
    char maps_file[64];
    snprintf(maps_file, sizeof(maps_file), "/proc/%d/maps", pid);
    FILE *mf = fopen(maps_file, "r");
    if (mf != NULL) { // Read maps file
        char line[2048];
        char **seen_mem = NULL; // Unique mapped paths
        int seen_count = 0;

        while (fgets(line, sizeof(line), mf) != NULL) { // Read each mapped region
            // Address perms offset dev inode pathname
            char *last_space = strrchr(line, ' ');
            if (last_space != NULL) {
                char *path = last_space + 1; // Extract pathname token
                // Trim trailing \n and \r
                size_t plen = strlen(path);
                while (plen > 0 && (path[plen - 1] == '\n' || path[plen - 1] == '\r')) {
                    path[--plen] = '\0';
                }

                if (plen > 0 && path[0] == '/') { // File path must be absolute
                    // Skip if identical to primary executable txt
                    if (exe_path[0] != '\0' && strcmp(path, exe_path) == 0) {
                        continue;
                    }
                    // Check if previously encountered in maps
                    bool already_seen = false;
                    for (int s = 0; s < seen_count; s++) {
                        if (strcmp(seen_mem[s], path) == 0) {
                            already_seen = true;
                            break;
                        }
                    }
                    if (already_seen) continue; // Deduplicate

                    // Add new mapped path to seen array
                    char **new_seen = realloc(seen_mem, (seen_count + 1) * sizeof(char *));
                    if (new_seen != NULL) {
                        seen_mem = new_seen;
                        seen_mem[seen_count++] = strdup(path);
                    }

                    struct stat st;
                    const char *t_str = "REG";
                    if (stat(path, &st) == 0) { // Stat file
                        t_str = get_file_type_str(st.st_mode, path);
                    }
                    printf("%-6d %-6s %-6s %s\n", pid, "mem", t_str, path); // Display mem row
                }
            }
        }
        fclose(mf); // Close maps stream

        // Free tracked mapped paths
        for (int s = 0; s < seen_count; s++) {
            free(seen_mem[s]);
        }
        free(seen_mem);
    }

    // 4. Inspect numeric file descriptors from /proc/<pid>/fd
    char fd_dir[64];
    snprintf(fd_dir, sizeof(fd_dir), "/proc/%d/fd", pid);
    DIR *d = opendir(fd_dir);
    if (d != NULL) { // Open fd directory
        struct dirent *de;
        int *fd_list = NULL; // Collected fd numbers
        int fd_count = 0;

        while ((de = readdir(d)) != NULL) { // Iterate directory entries
            if (de->d_name[0] == '.') continue; // Skip hidden entries
            bool is_num = true;
            for (int i = 0; de->d_name[i] != '\0'; i++) { // Verify all digits
                if (!isdigit((unsigned char)de->d_name[i])) {
                    is_num = false;
                    break;
                }
            }
            if (is_num) { // Append numeric fd
                int fnum = atoi(de->d_name);
                int *new_list = realloc(fd_list, (fd_count + 1) * sizeof(int));
                if (new_list != NULL) {
                    fd_list = new_list;
                    fd_list[fd_count++] = fnum;
                }
            }
        }
        closedir(d); // Close directory stream

        if (fd_count > 0) {
            // Sort file descriptor numbers ascending
            qsort(fd_list, fd_count, sizeof(int), compare_ints);

            // Print each file descriptor details
            for (int i = 0; i < fd_count; i++) {
                char link_path[128];
                snprintf(link_path, sizeof(link_path), "/proc/%d/fd/%d", pid, fd_list[i]);
                char target[1024];
                ssize_t rlen = readlink(link_path, target, sizeof(target) - 1);
                if (rlen > 0) { // Resolve fd target
                    target[rlen] = '\0';
                    struct stat st;
                    const char *t_str = "REG";
                    if (stat(link_path, &st) == 0) { // Stat target
                        t_str = get_file_type_str(st.st_mode, target);
                    } else if (strncmp(target, "pipe:", 5) == 0) { // Pipe descriptor
                        t_str = "FIFO";
                    } else if (strncmp(target, "socket:", 7) == 0) { // Socket descriptor
                        t_str = "SOCK";
                    }
                    char fd_num_str[32];
                    snprintf(fd_num_str, sizeof(fd_num_str), "%d", fd_list[i]);
                    printf("%-6d %-6s %-6s %s\n", pid, fd_num_str, t_str, target); // Display fd row
                }
            }
            free(fd_list); // Free collected fd buffer
        }
    }

    return 0; // Success
}
