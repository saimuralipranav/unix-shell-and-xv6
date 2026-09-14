#ifndef LEXER_H
#define LEXER_H

#include <stdbool.h>

// Lexical token categories recognized by c-shell
typedef enum {
    TOKEN_EOF = 0, // End of input token
    TOKEN_WORD,    // Shell command, argument, or file path
    TOKEN_PIPE,    // Pipeline separator '|'
    TOKEN_AMP,     // Background operator '&'
    TOKEN_SEMI,    // Command sequence separator ';'
    TOKEN_LT,      // Input redirection '<'
    TOKEN_GT,      // Output redirection '>'
    TOKEN_GTGT,    // Output append redirection '>>'
} TokenType;

// Token representation
typedef struct {
    TokenType type; // Classification of token
    char *value;    // Allocated string for WORD, NULL for operators
} Token;

// Frees the allocated memory inside the token array
void free_tokens(Token *tokens, int count);

// Lexes the line into tokens.
// Returns an allocated array of tokens on success, and sets out_count.
// If a lexical error occurs, prints error message, returns NULL, and out_count is set to -1.
Token *lex_line(const char *line, int *out_count);

#endif // LEXER_H
