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

typedef struct {
    Pipeline *pipelines;
    int pipeline_count;
} Sequence;

// Frees the parsed sequence
void free_sequence(Sequence *seq);

// Parses the token sequence and validates the grammar.
// If valid, populates the Sequence struct containing multiple Pipelines.
// If invalid, prints "cshell: invalid syntax", frees memory, and returns false.
bool parse_sequence(const Token *tokens, int count, Sequence *out_seq);

#endif // PARSER_H
