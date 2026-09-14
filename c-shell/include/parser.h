#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include <stdbool.h>

// Input redirection specifier (< file)
typedef struct {
    char *filepath; // Source input file path
} InputRedir;

// Output redirection specifier (> file or >> file)
typedef struct {
    char *filepath; // Destination output file path
    bool append;    // True for append mode (>>), false for overwrite (>)
} OutputRedir;

// Represents a single simple command with arguments and redirections
typedef struct {
    char **args;             // NULL-terminated argument vector
    int argc;                // Argument count
    InputRedir *in_redirs;   // List of input redirections (<)
    int in_redir_count;      // Number of input redirections
    OutputRedir *out_redirs; // List of output redirections (> and >>)
    int out_redir_count;     // Number of output redirections
} Command;

// Represents a pipeline of commands connected by '|'
typedef struct {
    Command *cmds;      // Array of commands in pipeline
    int cmd_count;      // Number of commands in pipeline
    bool is_background; // True if this pipeline is followed by '&'
} Pipeline;

// Top-level sequence of pipelines separated by ';'
typedef struct {
    Pipeline *pipelines; // Array of sequential pipelines
    int pipeline_count;  // Total number of pipelines
} Sequence;

// Frees the parsed sequence and all nested structures
void free_sequence(Sequence *seq);

// Parses the token sequence and validates the grammar.
// If valid, populates the Sequence struct containing multiple Pipelines.
// If invalid, prints "cshell: invalid syntax", frees memory, and returns false.
bool parse_sequence(const Token *tokens, int count, Sequence *out_seq);

#endif // PARSER_H
