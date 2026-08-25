#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Token *tokens;
    int capacity;
    int count;
} TokenList;

static void add_token(TokenList *list, TokenType type, char *val) {
    if (list->count >= list->capacity) {
        list->capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        Token *new_tokens = realloc(list->tokens, list->capacity * sizeof(Token));
        if (new_tokens == NULL) {
            perror("realloc");
            // Note: Caller handles freeing existing tokens on failure
            return;
        }
        list->tokens = new_tokens;
    }
    list->tokens[list->count++] = (Token){type, val};
}

void free_tokens(Token *tokens, int count) {
    if (tokens == NULL) return;
    for (int i = 0; i < count; i++) {
        if (tokens[i].type == TOKEN_WORD && tokens[i].value != NULL) {
            free(tokens[i].value);
        }
    }
    free(tokens);
}

Token *lex_line(const char *line, int *out_count) {
    TokenList list = {NULL, 0, 0};
    const char *p = line;

    while (true) {
        // 1. Skip spaces
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            p++;
        }

        if (*p == '\0') {
            break;
        }

        // 2. Operators
        if (*p == '|') {
            add_token(&list, TOKEN_PIPE, NULL);
            p++;
        } else if (*p == '&') {
            add_token(&list, TOKEN_AMP, NULL);
            p++;
        } else if (*p == ';') {
            add_token(&list, TOKEN_SEMI, NULL);
            p++;
        } else if (*p == '<') {
            add_token(&list, TOKEN_LT, NULL);
            p++;
        } else if (*p == '>') {
            if (*(p + 1) == '>') {
                add_token(&list, TOKEN_GTGT, NULL);
                p += 2;
            } else {
                add_token(&list, TOKEN_GT, NULL);
                p++;
            }
        } else {
            // 3. WORD (sequence of fragments)
            size_t max_word_len = strlen(p) + 1;
            char *buf = malloc(max_word_len);
            if (buf == NULL) {
                perror("malloc");
                free_tokens(list.tokens, list.count);
                *out_count = -1;
                return NULL;
            }
            size_t len = 0;
            bool lex_error = false;

            while (*p != '\0') {
                // Break on non-quoted, non-escaped special/space characters
                if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
                    *p == '|' || *p == '&' || *p == ';' || *p == '<' || *p == '>') {
                    break;
                }

                if (*p == '\\') {
                    p++; // consume '\\'
                    if (*p == '\0') {
                        fprintf(stderr, "cshell: invalid syntax\n");
                        lex_error = true;
                        break;
                    }
                    buf[len++] = *p;
                    p++;
                } else if (*p == '"') {
                    p++; // consume '"'
                    while (true) {
                        if (*p == '\0') {
                            fprintf(stderr, "cshell: invalid syntax\n");
                            lex_error = true;
                            break;
                        }
                        if (*p == '"') {
                            p++; // consume matching '"'
                            break;
                        }
                        if (*p == '\\') {
                            p++; // consume '\\'
                            if (*p == '\0') {
                                fprintf(stderr, "cshell: invalid syntax\n");
                                lex_error = true;
                                break;
                            }
                            if (*p == '"' || *p == '\\') {
                                buf[len++] = *p;
                            } else {
                                buf[len++] = '\\';
                                buf[len++] = *p;
                            }
                            p++;
                        } else {
                            buf[len++] = *p;
                            p++;
                        }
                    }
                    if (lex_error) break;
                } else if (*p == '\'') {
                    p++; // consume '\''
                    while (true) {
                        if (*p == '\0') {
                            fprintf(stderr, "cshell: invalid syntax\n");
                            lex_error = true;
                            break;
                        }
                        if (*p == '\'') {
                            p++; // consume matching '\''
                            break;
                        }
                        buf[len++] = *p;
                        p++;
                    }
                    if (lex_error) break;
                } else {
                    buf[len++] = *p;
                    p++;
                }
            }

            if (lex_error) {
                free(buf);
                free_tokens(list.tokens, list.count);
                *out_count = -1;
                return NULL;
            }

            buf[len] = '\0';
            add_token(&list, TOKEN_WORD, buf);
        }
    }

    add_token(&list, TOKEN_EOF, NULL);
    *out_count = list.count;
    return list.tokens;
}
