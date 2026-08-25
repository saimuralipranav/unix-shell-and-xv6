#include "shell.h"
#include "lexer.h"
#include "parser.h"
#include "exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    // Initialize shell global state (home directory, username, hostname)
    init_shell_state();

    char input[MAX_INPUT + 1];

    while (true) {
        // Display the prompt
        print_prompt();

        // Read user input
        if (fgets(input, sizeof(input), stdin) == NULL) {
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

        if (input_len > 0) {
            int count = 0;
            Token *tokens = lex_line(input, &count);
            if (tokens == NULL) {
                // Lexical error printed in lex_line
                continue;
            }

            Pipeline pipeline;
            if (parse_pipeline(tokens, count, &pipeline)) {
                if (pipeline.cmd_count > 0) {
                    execute_pipeline(&pipeline);
                }
                free_pipeline(&pipeline);
            }
            free_tokens(tokens, count);
        }
        clearerr(stdin);
    }

    return 0;
}
