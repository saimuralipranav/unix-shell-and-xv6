#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Dynamic array to accumulate parsed tokens
typedef struct {
    Token *tokens; // token array
    int capacity;  // array capacity
    int count;     // token count
} TokenList;

// Appends a token to the dynamic token list
static void add_token(TokenList *list, TokenType type, char *val) {
    // grow array capacity when full
    if (list->count >= list->capacity) {
        list->capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        Token *new_tokens = realloc(list->tokens, list->capacity * sizeof(Token));
        if (new_tokens == NULL) {
            perror("realloc"); // memory error
            return;
        }
        list->tokens = new_tokens; // update pointer
    }
    list->tokens[list->count++] = (Token){type, val}; // append token struct
}

// Frees token array and any dynamically allocated word values
void free_tokens(Token *tokens, int count) {
    if (tokens == NULL) return; // sanity check
    for (int i = 0; i < count; i++) {
        // free allocated word strings
        if (tokens[i].type == TOKEN_WORD && tokens[i].value != NULL) {
            free(tokens[i].value);
        }
    }
    free(tokens); // free token buffer
}

// Tokenizes a raw input line into a sequence of tokens
Token *lex_line(const char *line, int *out_count) {
    TokenList list = {NULL, 0, 0}; // initialize token list
    const char *p = line;          // input cursor

    // Tokenization loop
    while (true) {
        // 1. Skip whitespace characters
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            p++; // advance pointer
        }

        // Check for end of input line
        if (*p == '\0') {
            break; // finished
        }

        // 2. Recognize single and multi-character operators
        if (*p == '|') {
            add_token(&list, TOKEN_PIPE, NULL); // pipe '|'
            p++;
        } else if (*p == '&') {
            add_token(&list, TOKEN_AMP, NULL);  // background '&'
            p++;
        } else if (*p == ';') {
            add_token(&list, TOKEN_SEMI, NULL); // semicolon ';'
            p++;
        } else if (*p == '<') {
            add_token(&list, TOKEN_LT, NULL);   // input redirect '<'
            p++;
        } else if (*p == '>') {
            // Apply maximal munch: check for '>>' vs '>'
            if (*(p + 1) == '>') {
                add_token(&list, TOKEN_GTGT, NULL); // append redirect '>>'
                p += 2;
            } else {
                add_token(&list, TOKEN_GT, NULL);   // output redirect '>'
                p++;
            }
        } else {
            // 3. Parse WORD token composed of contiguous fragments
            size_t max_word_len = strlen(p) + 1; // maximum possible word length
            char *buf = malloc(max_word_len);    // allocate word buffer
            if (buf == NULL) {
                perror("malloc");
                free_tokens(list.tokens, list.count);
                *out_count = -1;
                return NULL;
            }
            size_t len = 0;        // word string length
            bool lex_error = false;// lexical error flag

            // Loop until whitespace or unquoted operator encountered
            while (*p != '\0') {
                // Break on non-quoted, non-escaped special/space characters
                if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
                    *p == '|' || *p == '&' || *p == ';' || *p == '<' || *p == '>') {
                    break;
                }

                // Handle backslash escape outside quotes
                if (*p == '\\') {
                    p++; // consume '\\'
                    if (*p == '\0') {
                        fprintf(stderr, "cshell: invalid syntax\n"); // trailing backslash error
                        lex_error = true;
                        break;
                    }
                    buf[len++] = *p; // append escaped character literally
                    p++;
                } else if (*p == '"') {
                    // Double-quoted string fragment
                    p++; // consume opening '"'
                    while (true) {
                        if (*p == '\0') {
                            fprintf(stderr, "cshell: invalid syntax\n"); // unclosed quote error
                            lex_error = true;
                            break;
                        }
                        if (*p == '"') {
                            p++; // consume matching closing '"'
                            break;
                        }
                        // Handle escape sequences inside double quotes
                        if (*p == '\\') {
                            p++; // consume '\\'
                            if (*p == '\0') {
                                fprintf(stderr, "cshell: invalid syntax\n");
                                lex_error = true;
                                break;
                            }
                            // \" and \\ are unescaped; all other \c retain both characters
                            if (*p == '"' || *p == '\\') {
                                buf[len++] = *p;
                            } else {
                                buf[len++] = '\\';
                                buf[len++] = *p;
                            }
                            p++;
                        } else {
                            buf[len++] = *p; // copy character verbatim
                            p++;
                        }
                    }
                    if (lex_error) break;
                } else if (*p == '\'') {
                    // Single-quoted string fragment (verbatim content)
                    p++; // consume opening '\''
                    while (true) {
                        if (*p == '\0') {
                            fprintf(stderr, "cshell: invalid syntax\n"); // unclosed quote error
                            lex_error = true;
                            break;
                        }
                        if (*p == '\'') {
                            p++; // consume matching closing '\''
                            break;
                        }
                        buf[len++] = *p; // verbatim copy, no escape processing
                        p++;
                    }
                    if (lex_error) break;
                } else {
                    // Ordinary character
                    buf[len++] = *p;
                    p++;
                }
            }

            // Clean up and fail on lexical error
            if (lex_error) {
                free(buf);
                free_tokens(list.tokens, list.count);
                *out_count = -1;
                return NULL;
            }

            buf[len] = '\0'; // null-terminate word
            add_token(&list, TOKEN_WORD, buf); // append word token
        }
    }

    // Append EOF terminal token
    add_token(&list, TOKEN_EOF, NULL);
    *out_count = list.count;
    return list.tokens; // return token list
}
