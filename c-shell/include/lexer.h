#ifndef LEXER_H
#define LEXER_H

#include <stdbool.h>

typedef enum {
    TOKEN_EOF = 0,
    TOKEN_WORD,
    TOKEN_PIPE,  // |
    TOKEN_AMP,   // &
    TOKEN_SEMI,  // ;
    TOKEN_LT,    // <
    TOKEN_GT,    // >
    TOKEN_GTGT,  // >>
} TokenType;

typedef struct {
    TokenType type;
    char *value; // Allocated string for WORD, NULL for operators
} Token;

// Frees the allocated memory inside the token array
void free_tokens(Token *tokens, int count);

// Lexes the line into tokens.
// Returns an allocated array of tokens on success, and sets out_count.
// If a lexical error occurs, prints error message, returns NULL, and out_count is set to -1.
Token *lex_line(const char *line, int *out_count);

#endif // LEXER_H
