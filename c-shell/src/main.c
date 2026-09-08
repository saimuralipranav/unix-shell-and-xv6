#include "shell.h"
#include "lexer.h"
#include "parser.h"
#include "exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

void sigchld_handler(int sig) {
    (void)sig;
    check_background_jobs();
}

int main(int argc, char *argv[]) {
    // Initialize shell global state (home directory, username, hostname)
    init_shell_state();
    
    // Set up SIGCHLD handler
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    char input[MAX_INPUT + 1];

    while (true) {
        // 1. Display the prompt
        print_prompt();

        // 2. Read user input
        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (errno == EINTR) {
                clearerr(stdin);
                printf("\n");
                continue;
            }
            // EOF (Ctrl+D) encountered, print a newline and exit cleanly
            printf("\n");
            break;
        }

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
