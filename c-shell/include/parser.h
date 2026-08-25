#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include <stdbool.h>

typedef struct {
    char *filepath;
} InputRedir;

typedef struct {
    char *filepath;
    bool append;
} OutputRedir;

typedef struct {
    char **args;
    int argc;
    InputRedir *in_redirs;
    int in_redir_count;
    OutputRedir *out_redirs;
    int out_redir_count;
} Command;

typedef struct {
    Command *cmds;
    int cmd_count;
} Pipeline;

// Frees the parsed pipeline
void free_pipeline(Pipeline *pipeline);

// Parses the token sequence and validates the grammar.
// If valid, populates the Pipeline struct for the FIRST command group (up to the first ; or &).
// If invalid, prints "cshell: invalid syntax", frees memory, and returns false.
bool parse_pipeline(const Token *tokens, int count, Pipeline *out_pipeline);

#endif // PARSER_H
