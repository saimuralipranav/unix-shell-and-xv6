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

static volatile sig_atomic_t sigint_received = 0;
static volatile sig_atomic_t sigtstp_received = 0;

void sigchld_handler(int sig) {
    (void)sig;
    check_background_jobs();
}

void sigint_handler(int sig) {
    (void)sig;
    sigint_received = 1;
}

void sigtstp_handler(int sig) {
    (void)sig;
    sigtstp_received = 1;
}

int main(int argc, char *argv[]) {
    // Initialize shell global state (home directory, username, hostname)
    init_shell_state();
    init_jobs();
    
    // Ignore SIGTTOU so tcsetpgrp does not stop shell
    signal(SIGTTOU, SIG_IGN);

    // Set up SIGTSTP handler so shell stays alive and redraws prompt
    struct sigaction sa_tstp;
    memset(&sa_tstp, 0, sizeof(sa_tstp));
    sa_tstp.sa_handler = sigtstp_handler;
    sigemptyset(&sa_tstp.sa_mask);
    sigaction(SIGTSTP, &sa_tstp, NULL);

    // Set up SIGINT handler so shell stays alive and redraws prompt
    struct sigaction sa_int;
    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sigaction(SIGINT, &sa_int, NULL);

    // Set up SIGCHLD handler
    struct sigaction sa_chld;
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    char input[MAX_INPUT + 1];
    bool prev_was_eof = false;

    while (true) {
        // 1. Display the prompt
        print_prompt();

        // 2. Read user input (handle partial input across Ctrl-D)
        size_t total_len = 0;
        input[0] = '\0';
        bool should_exit = false;

        while (total_len < MAX_INPUT) {
            if (fgets(input + total_len, sizeof(input) - total_len, stdin) == NULL) {
                if (errno == EINTR) {
                    clearerr(stdin);
                    if (sigint_received || sigtstp_received) {
                        sigint_received = 0;
                        sigtstp_received = 0;
                        printf("\n");
                        total_len = 0;
                        input[0] = '\0';
                        break;
                    }
                    continue;
                }

                // Ctrl-D on non-empty line: keep text and stay alive
                if (total_len > 0) {
                    clearerr(stdin);
                    continue;
                }

                // Ctrl-D on empty line
                if (has_stopped_jobs()) {
                    if (!prev_was_eof) {
                        printf("cshell: there are stopped jobs\n");
                        prev_was_eof = true;
                        clearerr(stdin);
                        break;
                    }
                }

                // EOF (Ctrl+D) exit
                printf("\n");
                sighup_all_jobs();
                should_exit = true;
                break;
            }

            total_len = strlen(input);
            if (total_len > 0 && (input[total_len - 1] == '\n' || input[total_len - 1] == '\r')) {
                break;
            }

            // If feof after reading without newline (Ctrl-D on partial line)
            if (feof(stdin)) {
                clearerr(stdin);
                continue;
            }
        }

        if (should_exit) {
            break;
        }

        if (total_len == 0) {
            continue;
        }

        prev_was_eof = false;

        // Trim trailing newlines and carriage returns
        size_t input_len = strlen(input);
        while (input_len > 0 && (input[input_len - 1] == '\n' || input[input_len - 1] == '\r')) {
            input[input_len - 1] = '\0';
            input_len--;
        }
        
        if (input_len == 0) continue;

        // 3. Lexical analysis
        int token_count = 0;
        Token *tokens = lex_line(input, &token_count);
        if (tokens == NULL) {
            // Lexical error occurred (already printed "cshell: invalid syntax")
            continue;
        }

        // 4. Grammar parsing and sequence construction
        Sequence seq;
        if (parse_sequence(tokens, token_count, &seq)) {
            // 5. Execution
            if (seq.pipeline_count > 0) {
                execute_sequence(&seq);
            }
            free_sequence(&seq);
        }

        // 6. Free tokens
        free_tokens(tokens, token_count);
    }

    return 0;
}
