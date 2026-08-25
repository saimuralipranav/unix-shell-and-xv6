#include "peek.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// ---------------------------------------------------------
// Intrinsic: peek
// ---------------------------------------------------------
typedef struct {
    off_t offset;
    size_t len;
    int line_num;
} LineInfo;

static int peek_forward(const char *filename, bool show_lines, int *line_counter) {
    FILE *fp;
    if (strcmp(filename, "-") == 0) {
        fp = stdin;
    } else {
        fp = fopen(filename, "r");
        if (fp == NULL) {
            fprintf(stderr, "peek: no such file or directory\n");
            return 1;
        }
    }

    char line[4096];
    while (fgets(line, sizeof(line), fp) != NULL) {
        bool non_empty = false;
        for (int i = 0; line[i] != '\0'; i++) {
            if (line[i] != '\n' && line[i] != '\r') {
                non_empty = true;
                break;
            }
        }

        if (show_lines && non_empty) {
            (*line_counter)++;
            printf("%d %s", *line_counter, line);
        } else {
            printf("%s", line);
        }
    }

    if (fp != stdin) {
        fclose(fp);
    }
    return 0;
}

static int peek_reversed(const char *filename, bool show_lines, int *line_counter) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "peek: no such file or directory\n");
        return 1;
    }

    LineInfo *lines = NULL;
    int line_count = 0;
    int line_capacity = 0;

    off_t line_start = 0;
    off_t cur_pos = 0;
    bool has_non_space = false;

    char buf[4096];
    ssize_t bytes_read;
    while ((bytes_read = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < bytes_read; i++) {
            char c = buf[i];
            if (c != '\n' && c != '\r') {
                has_non_space = true;
            }
            if (c == '\n') {
                size_t len = (cur_pos + i) - line_start + 1;
                if (line_count >= line_capacity) {
                    line_capacity = line_capacity == 0 ? 128 : line_capacity * 2;
                    LineInfo *new_lines = realloc(lines, line_capacity * sizeof(LineInfo));
                    if (new_lines == NULL) {
                        perror("realloc");
                        close(fd);
                        free(lines);
                        return 1;
                    }
                    lines = new_lines;
                }
                int l_num = 0;
                if (has_non_space) {
                    (*line_counter)++;
                    l_num = *line_counter;
                }
                lines[line_count++] = (LineInfo){line_start, len, l_num};
                line_start = cur_pos + i + 1;
                has_non_space = false;
            }
        }
        cur_pos += bytes_read;
    }

    if (cur_pos > line_start) {
        size_t len = cur_pos - line_start;
        if (line_count >= line_capacity) {
            line_capacity = line_capacity == 0 ? 128 : line_capacity * 2;
            LineInfo *new_lines = realloc(lines, line_capacity * sizeof(LineInfo));
            if (new_lines == NULL) {
                perror("realloc");
                close(fd);
                free(lines);
                return 1;
            }
            lines = new_lines;
        }
        int l_num = 0;
        if (has_non_space) {
            (*line_counter)++;
            l_num = *line_counter;
        }
        lines[line_count++] = (LineInfo){line_start, len, l_num};
    }

    for (int i = line_count - 1; i >= 0; i--) {
        lseek(fd, lines[i].offset, SEEK_SET);
        char *line_buf = malloc(lines[i].len + 1);
        if (line_buf != NULL) {
            ssize_t r = read(fd, line_buf, lines[i].len);
            if (r > 0) {
                line_buf[r] = '\0';
                if (show_lines && lines[i].line_num > 0) {
                    printf("%d %s", lines[i].line_num, line_buf);
                } else {
                    printf("%s", line_buf);
                }
            }
            free(line_buf);
        }
    }
    free(lines);
    close(fd);
    return 0;
}

static int peek_stdin_reversed(bool show_lines, int *line_counter) {
    char **lines = NULL;
    int count = 0;
    int capacity = 0;

    char line[4096];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        if (count >= capacity) {
            capacity = capacity == 0 ? 128 : capacity * 2;
            char **new_lines = realloc(lines, capacity * sizeof(char *));
            if (new_lines == NULL) {
                perror("realloc");
                for (int i = 0; i < count; i++) free(lines[i]);
                free(lines);
                return 1;
            }
            lines = new_lines;
        }
        lines[count++] = strdup(line);
    }

    int *line_nums = malloc(count * sizeof(int));
    if (line_nums == NULL) {
        perror("malloc");
        for (int i = 0; i < count; i++) free(lines[i]);
        free(lines);
        return 1;
    }
    for (int i = 0; i < count; i++) {
        bool non_empty = false;
        for (int j = 0; lines[i][j] != '\0'; j++) {
            if (lines[i][j] != '\n' && lines[i][j] != '\r') {
                non_empty = true;
                break;
            }
        }
        if (non_empty) {
            (*line_counter)++;
            line_nums[i] = *line_counter;
        } else {
            line_nums[i] = 0;
        }
    }

    for (int i = count - 1; i >= 0; i--) {
        if (show_lines && line_nums[i] > 0) {
            printf("%d %s", line_nums[i], lines[i]);
        } else {
            printf("%s", lines[i]);
        }
        free(lines[i]);
    }
    free(lines);
    free(line_nums);
    return 0;
}

int builtin_peek(Command *cmd) {
    bool show_lines = false;
    bool reverse = false;
    char **filenames = NULL;
    int file_count = 0;
    int file_capacity = 0;

    for (int i = 1; i < cmd->argc; i++) {
        const char *arg = cmd->args[i];
        if (arg[0] == '-' && strlen(arg) > 1) {
            for (int j = 1; arg[j] != '\0'; j++) {
                if (arg[j] == 'n') {
                    show_lines = true;
                } else if (arg[j] == 'r') {
                    reverse = true;
                } else {
                    fprintf(stderr, "peek: invalid syntax\n");
                    free(filenames);
                    return 1;
                }
            }
        } else {
            if (file_count >= file_capacity) {
                file_capacity = file_capacity == 0 ? 8 : file_capacity * 2;
                char **new_files = realloc(filenames, file_capacity * sizeof(char *));
                if (new_files == NULL) {
                    perror("realloc");
                    free(filenames);
                    return 1;
                }
                filenames = new_files;
            }
            filenames[file_count++] = (char *)arg;
        }
    }

    // Default to stdin if no file given
    if (file_count == 0) {
        if (file_count >= file_capacity) {
            file_capacity = 8;
            filenames = realloc(filenames, file_capacity * sizeof(char *));
        }
        filenames[file_count++] = "-";
    }

    // Pre-validate files
    for (int i = 0; i < file_count; i++) {
        if (strcmp(filenames[i], "-") != 0) {
            struct stat st;
            if (stat(filenames[i], &st) != 0) {
                fprintf(stderr, "peek: no such file or directory\n");
                free(filenames);
                return 1;
            }
            if (S_ISDIR(st.st_mode)) {
                fprintf(stderr, "peek: is a directory\n");
                free(filenames);
                return 1;
            }
        }
    }

    int line_counter = 0;
    int exit_status = 0;

    for (int i = 0; i < file_count; i++) {
        if (reverse) {
            if (strcmp(filenames[i], "-") == 0) {
                exit_status = peek_stdin_reversed(show_lines, &line_counter);
            } else {
                exit_status = peek_reversed(filenames[i], show_lines, &line_counter);
            }
        } else {
            exit_status = peek_forward(filenames[i], show_lines, &line_counter);
        }
        if (exit_status != 0) {
            break;
        }
    }

    free(filenames);
    return exit_status;
}
