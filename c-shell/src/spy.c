#include "spy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>

static const char *get_file_type_str(mode_t mode, const char *path) {
    if (S_ISDIR(mode)) return "DIR";
    if (S_ISREG(mode)) return "REG";
    if (S_ISCHR(mode)) return "CHR";
    if (S_ISBLK(mode)) return "BLK";
    if (S_ISFIFO(mode)) return "FIFO";
    if (S_ISSOCK(mode)) return "SOCK";
    if (S_ISLNK(mode)) return "LNK";
    if (path != NULL && strncmp(path, "pipe:", 5) == 0) return "FIFO";
    if (path != NULL && strncmp(path, "socket:", 7) == 0) return "SOCK";
    return "REG";
}

static int compare_ints(const void *a, const void *b) {
    return (*(const int *)a - *(const int *)b);
}

int builtin_spy(Command *cmd) {
    if (cmd == NULL) return 1;

    if (cmd->argc > 2) {
        fprintf(stderr, "spy: invalid syntax\n");
        return 1;
    }

    pid_t pid = getpid();
    if (cmd->argc == 2) {
        // Validate pid string
        const char *arg = cmd->args[1];
        for (int i = 0; arg[i] != '\0'; i++) {
            if (!isdigit((unsigned char)arg[i])) {
                fprintf(stderr, "spy: invalid syntax\n");
                return 1;
            }
        }
        pid = (pid_t)atoi(arg);
        if (pid <= 0) {
            fprintf(stderr, "spy: no such process\n");
            return 1;
        }
    }

    char proc_dir[64];
    snprintf(proc_dir, sizeof(proc_dir), "/proc/%d", pid);
    struct stat proc_st;
    if (stat(proc_dir, &proc_st) != 0 || !S_ISDIR(proc_st.st_mode)) {
        fprintf(stderr, "spy: no such process\n");
        return 1;
    }

    printf("%-6s %-6s %-6s %s\n", "PID", "FD", "TYPE", "PATH");

    // 1. cwd
    char cwd_link[64];
    snprintf(cwd_link, sizeof(cwd_link), "/proc/%d/cwd", pid);
    char cwd_path[1024];
    ssize_t len = readlink(cwd_link, cwd_path, sizeof(cwd_path) - 1);
    if (len > 0) {
        cwd_path[len] = '\0';
        struct stat st;
        const char *t_str = "DIR";
        if (stat(cwd_link, &st) == 0) {
            t_str = get_file_type_str(st.st_mode, cwd_path);
        }
        printf("%-6d %-6s %-6s %s\n", pid, "cwd", t_str, cwd_path);
    }

    // 2. txt (executable)
    char exe_link[64];
    snprintf(exe_link, sizeof(exe_link), "/proc/%d/exe", pid);
    char exe_path[1024];
    exe_path[0] = '\0';
    len = readlink(exe_link, exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';
        struct stat st;
        const char *t_str = "REG";
        if (stat(exe_link, &st) == 0) {
            t_str = get_file_type_str(st.st_mode, exe_path);
        }
        printf("%-6d %-6s %-6s %s\n", pid, "txt", t_str, exe_path);
    }

    // 3. mem (memory mapped files from /proc/<pid>/maps)
    char maps_file[64];
    snprintf(maps_file, sizeof(maps_file), "/proc/%d/maps", pid);
    FILE *mf = fopen(maps_file, "r");
    if (mf != NULL) {
        char line[2048];
        char **seen_mem = NULL;
        int seen_count = 0;

        while (fgets(line, sizeof(line), mf) != NULL) {
            // Address perms offset dev inode pathname
            char *last_space = strrchr(line, ' ');
            if (last_space != NULL) {
                char *path = last_space + 1;
                // Trim trailing \n and \r
                size_t plen = strlen(path);
                while (plen > 0 && (path[plen - 1] == '\n' || path[plen - 1] == '\r')) {
                    path[--plen] = '\0';
                }

                if (plen > 0 && path[0] == '/') {
                    // Check if already printed as txt or seen in mem
                    if (exe_path[0] != '\0' && strcmp(path, exe_path) == 0) {
                        continue;
                    }
                    bool already_seen = false;
                    for (int s = 0; s < seen_count; s++) {
                        if (strcmp(seen_mem[s], path) == 0) {
                            already_seen = true;
                            break;
                        }
                    }
                    if (already_seen) continue;

                    // Add to seen
                    char **new_seen = realloc(seen_mem, (seen_count + 1) * sizeof(char *));
                    if (new_seen != NULL) {
                        seen_mem = new_seen;
                        seen_mem[seen_count++] = strdup(path);
                    }

                    struct stat st;
                    const char *t_str = "REG";
                    if (stat(path, &st) == 0) {
                        t_str = get_file_type_str(st.st_mode, path);
                    }
                    printf("%-6d %-6s %-6s %s\n", pid, "mem", t_str, path);
                }
            }
        }
        fclose(mf);

        for (int s = 0; s < seen_count; s++) {
            free(seen_mem[s]);
        }
        free(seen_mem);
    }

    // 4. Numeric file descriptors from /proc/<pid>/fd
    char fd_dir[64];
    snprintf(fd_dir, sizeof(fd_dir), "/proc/%d/fd", pid);
    DIR *d = opendir(fd_dir);
    if (d != NULL) {
        struct dirent *de;
        int *fd_list = NULL;
        int fd_count = 0;

        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] == '.') continue;
            bool is_num = true;
            for (int i = 0; de->d_name[i] != '\0'; i++) {
                if (!isdigit((unsigned char)de->d_name[i])) {
                    is_num = false;
                    break;
                }
            }
            if (is_num) {
                int fnum = atoi(de->d_name);
                int *new_list = realloc(fd_list, (fd_count + 1) * sizeof(int));
                if (new_list != NULL) {
                    fd_list = new_list;
                    fd_list[fd_count++] = fnum;
                }
            }
        }
        closedir(d);

        if (fd_count > 0) {
            qsort(fd_list, fd_count, sizeof(int), compare_ints);

            for (int i = 0; i < fd_count; i++) {
                char link_path[128];
                snprintf(link_path, sizeof(link_path), "/proc/%d/fd/%d", pid, fd_list[i]);
                char target[1024];
                ssize_t rlen = readlink(link_path, target, sizeof(target) - 1);
                if (rlen > 0) {
                    target[rlen] = '\0';
                    struct stat st;
                    const char *t_str = "REG";
                    if (stat(link_path, &st) == 0) {
                        t_str = get_file_type_str(st.st_mode, target);
                    } else if (strncmp(target, "pipe:", 5) == 0) {
                        t_str = "FIFO";
                    } else if (strncmp(target, "socket:", 7) == 0) {
                        t_str = "SOCK";
                    }
                    char fd_num_str[32];
                    snprintf(fd_num_str, sizeof(fd_num_str), "%d", fd_list[i]);
                    printf("%-6d %-6s %-6s %s\n", pid, fd_num_str, t_str, target);
                }
            }
            free(fd_list);
        }
    }

    return 0;
}
