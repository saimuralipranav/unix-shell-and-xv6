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
// Tracks metadata for each line to enable lseek-based reverse reading
typedef struct {
    off_t offset; // file byte offset of line start
    size_t len;   // length in bytes
    int line_num; // assigned non-empty line number
} LineInfo;

// Reads file or stdin forward, optionally numbering non-empty lines
static int peek_forward(const char *filename, bool show_lines, int *line_counter) {
    FILE *fp;
    // Check if reading from standard input or regular file
    if (strcmp(filename, "-") == 0) {
        fp = stdin; // standard input stream
    } else {
        fp = fopen(filename, "r"); // open file
        if (fp == NULL) {
            fprintf(stderr, "peek: no such file or directory\n"); // open error
            return 1;
        }
    }

    char line[4096]; // line read buffer
    // Read lines sequentially
    while (fgets(line, sizeof(line), fp) != NULL) {
        bool non_empty = false; // check if line contains content
        for (int i = 0; line[i] != '\0'; i++) {
            if (line[i] != '\n' && line[i] != '\r') {
                non_empty = true; // non-blank character found
                break;
            }
        }

        // Only increment and print number for non-empty lines
        if (show_lines && non_empty) {
            (*line_counter)++;                   // increment counter
            printf("%d %s", *line_counter, line);// print with line number
        } else {
            printf("%s", line);                  // print raw line
        }
    }

    // Close regular file descriptor
    if (fp != stdin) {
        fclose(fp);
    }
    return 0; // success
}

// Reads seekable regular file backwards using lseek chunking
static int peek_reversed(const char *filename, bool show_lines, int *line_counter) {
    int fd = open(filename, O_RDONLY); // open file descriptor
    if (fd < 0) {
        fprintf(stderr, "peek: no such file or directory\n"); // error
        return 1;
    }

    LineInfo *lines = NULL; // array of line boundaries
    int line_count = 0;     // line count
    int line_capacity = 0;  // capacity

    off_t line_start = 0;   // start offset of current line
    off_t cur_pos = 0;      // current file offset
    bool has_non_space = false;

    char buf[4096]; // chunk read buffer
    ssize_t bytes_read;
    // Scan file to record line offsets and assign numbers
    while ((bytes_read = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < bytes_read; i++) {
            char c = buf[i];
            if (c != '\n' && c != '\r') {
                has_non_space = true; // non-empty char
            }
            if (c == '\n') {
                size_t len = (cur_pos + i) - line_start + 1; // calculate line length
                // resize line array if necessary
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
                // assign line number based on original position
                if (has_non_space) {
                    (*line_counter)++;
                    l_num = *line_counter;
                }
                lines[line_count++] = (LineInfo){line_start, len, l_num}; // store record
                line_start = cur_pos + i + 1;                            // next line start
                has_non_space = false;                                   // reset flag
            }
        }
        cur_pos += bytes_read; // update file position
    }

    // Handle last line without trailing newline
    if (cur_pos > line_start) {
        size_t len = cur_pos - line_start; // length of remaining bytes
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

    // Read and print lines in reverse order using lseek
    for (int i = line_count - 1; i >= 0; i--) {
        lseek(fd, lines[i].offset, SEEK_SET); // seek to line start
        char *line_buf = malloc(lines[i].len + 1);
        if (line_buf != NULL) {
            ssize_t r = read(fd, line_buf, lines[i].len); // read line bytes
            if (r > 0) {
                line_buf[r] = '\0'; // null-terminate
                // print with line number if non-empty
                if (show_lines && lines[i].line_num > 0) {
                    printf("%d %s", lines[i].line_num, line_buf);
                } else {
                    printf("%s", line_buf);
                }
            }
            free(line_buf); // free line buffer
        }
    }
    free(lines); // free metadata
    close(fd);   // close file
    return 0;    // success
}

// Reverses unseekable standard input by buffering lines
static int peek_stdin_reversed(bool show_lines, int *line_counter) {
    char **lines = NULL; // dynamic line array
    int count = 0;       // line count
    int capacity = 0;    // capacity

    char line[4096];
    // Buffer all lines from stdin
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
        lines[count++] = strdup(line); // copy line
    }

    // Assign line numbers in forward order
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
            line_nums[i] = *line_counter; // assign forward number
        } else {
            line_nums[i] = 0; // empty line gets no number
        }
    }

    // Print buffered lines in reverse order
    for (int i = count - 1; i >= 0; i--) {
        if (show_lines && line_nums[i] > 0) {
            printf("%d %s", line_nums[i], lines[i]);
        } else {
            printf("%s", lines[i]);
        }
        free(lines[i]); // free line string
    }
    free(lines);     // free array
    free(line_nums); // free numbers array
    return 0;        // success
}

// Builtin 'peek': prints file contents forward or backward
int builtin_peek(Command *cmd) {
    bool show_lines = false; // -n flag
    bool reverse = false;    // -r flag
    char **filenames = NULL; // array of filenames
    int file_count = 0;      // count of target files
    int file_capacity = 0;   // capacity

    // Parse options (-n, -r) and filenames
    for (int i = 1; i < cmd->argc; i++) {
        const char *arg = cmd->args[i];
        if (arg[0] == '-' && strlen(arg) > 1) {
            // parse flags
            for (int j = 1; arg[j] != '\0'; j++) {
                if (arg[j] == 'n') {
                    show_lines = true; // line numbers
                } else if (arg[j] == 'r') {
                    reverse = true;    // reverse order
                } else {
                    fprintf(stderr, "peek: invalid syntax\n"); // invalid flag
                    free(filenames);
                    return 1;
                }
            }
        } else {
            // record filename argument
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
            filenames[file_count++] = (char *)arg; // store filename
        }
    }

    // Default to stdin if no file arguments were given
    if (file_count == 0) {
        if (file_count >= file_capacity) {
            file_capacity = 8;
            filenames = realloc(filenames, file_capacity * sizeof(char *));
        }
        filenames[file_count++] = "-"; // stdin marker
    }

    int line_counter = 0; // global non-empty line counter across all files
    int exit_status = 0;  // overall exit status

    // Process each specified file sequentially
    for (int i = 0; i < file_count; i++) {
        // Validate regular file and ensure it is not a directory
        if (strcmp(filenames[i], "-") != 0) {
            struct stat st;
            if (stat(filenames[i], &st) != 0) {
                fprintf(stderr, "peek: no such file or directory\n"); // file missing
                exit_status = 1;
                continue; // continue processing remaining files
            }
            if (S_ISDIR(st.st_mode)) {
                fprintf(stderr, "peek: is a directory\n"); // directory error
                exit_status = 1;
                continue; // continue processing remaining files
            }
        }

        int res = 0;
        // Dispatch to appropriate reading routine
        if (reverse) {
            if (strcmp(filenames[i], "-") == 0) {
                res = peek_stdin_reversed(show_lines, &line_counter); // stdin reversed
            } else {
                res = peek_reversed(filenames[i], show_lines, &line_counter); // seekable file reversed
            }
        } else {
            res = peek_forward(filenames[i], show_lines, &line_counter); // forward read
        }
        if (res != 0) {
            exit_status = 1; // record error
        }
    }

    free(filenames);    // free filename list
    return exit_status; // return final exit code
}
