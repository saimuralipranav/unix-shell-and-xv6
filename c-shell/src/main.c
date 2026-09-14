#include "shell.h"
#include "lexer.h"
#include "parser.h"
#include "exec.h"
#include "jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

static volatile sig_atomic_t sigint_received = 0;  // flag for SIGINT (Ctrl-C)
static volatile sig_atomic_t sigtstp_received = 0; // flag for SIGTSTP (Ctrl-Z)

// Signal handler for SIGCHLD: reaps background children
void sigchld_handler(int sig) {
    (void)sig;                // unused signal number
    check_background_jobs();  // non-blocking reap and buffer notifications
}

// Signal handler for SIGINT: flags Ctrl-C without killing shell
void sigint_handler(int sig) {
    (void)sig;                // unused signal number
    sigint_received = 1;      // mark signal received
}

// Signal handler for SIGTSTP: flags Ctrl-Z without stopping shell
void sigtstp_handler(int sig) {
    (void)sig;                // unused signal number
    sigtstp_received = 1;     // mark signal received
}

int main(int argc, char *argv[]) {
    (void)argc; // unused argc
    (void)argv; // unused argv

    // Initialize shell global state (home directory, username, hostname)
    init_shell_state();
    init_jobs(); // initialize jobs tracking table
    
    // Ignore SIGTTOU so tcsetpgrp does not stop shell
    signal(SIGTTOU, SIG_IGN);

    // Set up SIGTSTP handler so shell stays alive and redraws prompt
    struct sigaction sa_tstp;
    memset(&sa_tstp, 0, sizeof(sa_tstp));
    sa_tstp.sa_handler = sigtstp_handler; // install handler
    sigemptyset(&sa_tstp.sa_mask);        // clear mask
    sigaction(SIGTSTP, &sa_tstp, NULL);   // register action

    // Set up SIGINT handler so shell stays alive and redraws prompt
    struct sigaction sa_int;
    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = sigint_handler;   // install handler
    sigemptyset(&sa_int.sa_mask);        // clear mask
    sigaction(SIGINT, &sa_int, NULL);     // register action

    // Set up SIGCHLD handler for background job reaping
    struct sigaction sa_chld;
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = sigchld_handler; // install handler
    sigemptyset(&sa_chld.sa_mask);        // clear mask
    sa_chld.sa_flags = SA_RESTART;        // auto-restart interrupted syscalls
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("sigaction");              // error registering handler
        exit(1);
    }

    char input[MAX_INPUT + 1];  // input buffer
    bool prev_was_eof = false;  // tracks consecutive Ctrl-D presses

    // Main REPL loop
    while (true) {
        // 1. Flush background job completions before prompt
        flush_background_notifications();

        // 2. Display the prompt
        print_prompt();

        // 3. Read user input (handle partial input across Ctrl-D)
        size_t total_len = 0;       // length of accumulated input
        input[0] = '\0';            // clear buffer
        bool should_exit = false;   // exit request flag

        // Accumulate input line
        while (total_len < MAX_INPUT) {
            errno = 0; // reset errno before reading
            // read chunk from stdin
            if (fgets(input + total_len, sizeof(input) - total_len, stdin) == NULL) {
                // handle interrupted read via signal
                if (ferror(stdin) && errno == EINTR) {
                    clearerr(stdin); // clear error indicator
                    // if Ctrl-C or Ctrl-Z was pressed, print newline and discard partial line
                    if (sigint_received || sigtstp_received) {
                        sigint_received = 0;  // reset flag
                        sigtstp_received = 0; // reset flag
                        printf("\n");         // newline
                        total_len = 0;        // reset length
                        input[0] = '\0';      // clear input
                        break;                // redraw prompt
                    }
                    continue; // restart read
                }

                // Ctrl-D on non-empty line: keep text and stay alive
                if (total_len > 0) {
                    clearerr(stdin); // clear EOF indicator
                    continue;        // continue reading
                }

                // Ctrl-D on empty line with stopped jobs
                if (has_stopped_jobs()) {
                    if (!prev_was_eof) {
                        printf("cshell: there are stopped jobs\n"); // warn user
                        prev_was_eof = true;                        // record EOF press
                        clearerr(stdin);                            // clear EOF
                        break;                                      // redraw prompt
                    }
                }

                // EOF (Ctrl+D) exit confirmed
                printf("\n");
                sighup_all_jobs();  // notify tracked jobs with SIGHUP
                should_exit = true; // request termination
                break;
            }

            total_len = strlen(input); // update total length
            // break if newline received
            if (total_len > 0 && (input[total_len - 1] == '\n' || input[total_len - 1] == '\r')) {
                break;
            }

            // If feof after reading without newline (Ctrl-D on partial line)
            if (feof(stdin)) {
                clearerr(stdin); // clear EOF indicator
                continue;        // keep waiting for more input
            }
        }

        // Check if shell should terminate
        if (should_exit) {
            break; // exit REPL
        }

        // Empty line received
        if (total_len == 0) {
            continue; // redraw prompt
        }

        prev_was_eof = false; // reset consecutive EOF flag

        // Trim trailing newlines and carriage returns
        size_t input_len = strlen(input);
        while (input_len > 0 && (input[input_len - 1] == '\n' || input[input_len - 1] == '\r')) {
            input[input_len - 1] = '\0'; // trim char
            input_len--;                 // decrement length
        }
        
        if (input_len == 0) continue; // blank line after trimming

        // 4. Lexical analysis: tokenize input
        int token_count = 0;
        Token *tokens = lex_line(input, &token_count);
        if (tokens == NULL) {
            // Lexical error occurred (already printed "cshell: invalid syntax")
            continue;
        }

        // 5. Grammar parsing and sequence construction
        Sequence seq;
        if (parse_sequence(tokens, token_count, &seq)) {
            // 6. Execute sequence of pipelines
            if (seq.pipeline_count > 0) {
                execute_sequence(&seq); // execute pipelines
            }
            free_sequence(&seq); // free sequence memory
        }

        // 7. Free allocated tokens
        free_tokens(tokens, token_count);
    }

    return 0; // exit shell cleanly
}
