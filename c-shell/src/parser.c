#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Parser state tracking token stream cursor
typedef struct {
    const Token *tokens; // token array
    int count;           // token count
    int idx;             // current index
} ParserState;

// Peeks at current token without advancing cursor
static inline const Token *peek_tok(ParserState *state) {
    if (state->idx >= state->count) {
        return NULL; // past end
    }
    return &state->tokens[state->idx]; // current token
}

// Advances cursor and returns consumed token
static inline const Token *advance_tok(ParserState *state) {
    if (state->idx >= state->count) {
        return NULL; // past end
    }
    return &state->tokens[state->idx++]; // return and advance
}

// Recursive descent grammar non-terminal forward declarations
static bool parse_LINE(ParserState *state);
static bool parse_ARG(ParserState *state);
static bool parse_CMD(ParserState *state);
static bool parse_TGT(ParserState *state);
static bool parse_BG(ParserState *state);

// Grammar rule: LINE -> ε | WORD ARG
static bool parse_LINE(ParserState *state) {
    const Token *t = peek_tok(state);
    if (t == NULL) return false;

    // ε production on EOF
    if (t->type == TOKEN_EOF) {
        return true;
    }
    // WORD ARG production
    if (t->type == TOKEN_WORD) {
        advance_tok(state); // consume WORD
        return parse_ARG(state); // parse ARG
    }
    return false; // syntax error
}

// Grammar rule: ARG -> ε | WORD ARG | < TGT | > TGT | >> TGT | | CMD | ; CMD | & BG
static bool parse_ARG(ParserState *state) {
    const Token *t = peek_tok(state);
    if (t == NULL) return false;

    // ε production on EOF
    if (t->type == TOKEN_EOF) {
        return true;
    }
    // WORD ARG
    if (t->type == TOKEN_WORD) {
        advance_tok(state); // consume WORD
        return parse_ARG(state);
    }
    // Input redirection: < TGT
    if (t->type == TOKEN_LT) {
        advance_tok(state); // consume '<'
        return parse_TGT(state);
    }
    // Output redirection: > TGT or >> TGT
    if (t->type == TOKEN_GT || t->type == TOKEN_GTGT) {
        advance_tok(state); // consume '>' or '>>'
        return parse_TGT(state);
    }
    // Pipeline operator: | CMD
    if (t->type == TOKEN_PIPE) {
        advance_tok(state); // consume '|'
        return parse_CMD(state);
    }
    // Sequential operator: ; CMD
    if (t->type == TOKEN_SEMI) {
        advance_tok(state); // consume ';'
        return parse_CMD(state);
    }
    // Background operator: & BG
    if (t->type == TOKEN_AMP) {
        advance_tok(state); // consume '&'
        return parse_BG(state);
    }
    return false; // syntax error
}

// Grammar rule: CMD -> WORD ARG
static bool parse_CMD(ParserState *state) {
    const Token *t = peek_tok(state);
    if (t == NULL) return false;

    if (t->type == TOKEN_WORD) {
        advance_tok(state); // consume WORD
        return parse_ARG(state); // parse ARG
    }
    return false; // missing command name after operator
}

// Grammar rule: TGT -> WORD ARG (target of redirection)
static bool parse_TGT(ParserState *state) {
    const Token *t = peek_tok(state);
    if (t == NULL) return false;

    if (t->type == TOKEN_WORD) {
        advance_tok(state); // consume redirect target filename
        return parse_ARG(state); // continue parsing arguments
    }
    return false; // missing redirection target
}

// Grammar rule: BG -> ε | WORD ARG (command after background &)
static bool parse_BG(ParserState *state) {
    const Token *t = peek_tok(state);
    if (t == NULL) return false;

    // ε production on EOF
    if (t->type == TOKEN_EOF) {
        return true;
    }
    // WORD ARG production
    if (t->type == TOKEN_WORD) {
        advance_tok(state); // consume WORD
        return parse_ARG(state);
    }
    return false; // invalid token following '&'
}

// Frees all commands, arguments, and redirections inside a pipeline
static void free_pipeline(Pipeline *pipeline) {
    if (pipeline == NULL || pipeline->cmds == NULL) return;
    for (int i = 0; i < pipeline->cmd_count; i++) {
        Command *cmd = &pipeline->cmds[i];
        // free argument strings
        if (cmd->args != NULL) {
            for (int j = 0; j < cmd->argc; j++) {
                free(cmd->args[j]);
            }
            free(cmd->args);
        }
        // free input redirection paths
        if (cmd->in_redirs != NULL) {
            for (int j = 0; j < cmd->in_redir_count; j++) {
                free(cmd->in_redirs[j].filepath);
            }
            free(cmd->in_redirs);
        }
        // free output redirection paths
        if (cmd->out_redirs != NULL) {
            for (int j = 0; j < cmd->out_redir_count; j++) {
                free(cmd->out_redirs[j].filepath);
            }
            free(cmd->out_redirs);
        }
    }
    free(pipeline->cmds); // free command array
    pipeline->cmds = NULL;
    pipeline->cmd_count = 0;
}

// Frees an entire parsed sequence of pipelines
void free_sequence(Sequence *seq) {
    if (seq == NULL || seq->pipelines == NULL) return;
    for (int i = 0; i < seq->pipeline_count; i++) {
        free_pipeline(&seq->pipelines[i]); // free pipeline
    }
    free(seq->pipelines); // free pipeline array
    seq->pipelines = NULL;
    seq->pipeline_count = 0;
}

// Initializes a Command struct with empty fields
static void init_command(Command *cmd) {
    cmd->args = NULL;
    cmd->argc = 0;
    cmd->in_redirs = NULL;
    cmd->in_redir_count = 0;
    cmd->out_redirs = NULL;
    cmd->out_redir_count = 0;
}

// Appends an argument string to a Command
static void add_arg(Command *cmd, const char *arg) {
    char **new_args = realloc(cmd->args, (cmd->argc + 2) * sizeof(char *));
    if (new_args == NULL) {
        perror("realloc");
        return;
    }
    cmd->args = new_args;
    cmd->args[cmd->argc] = strdup(arg); // copy argument
    cmd->argc++;
    cmd->args[cmd->argc] = NULL;        // null-terminate argv array
}

// Appends an input redirection target to a Command
static void add_in_redir(Command *cmd, const char *path) {
    InputRedir *new_in = realloc(cmd->in_redirs, (cmd->in_redir_count + 1) * sizeof(InputRedir));
    if (new_in == NULL) {
        perror("realloc");
        return;
    }
    cmd->in_redirs = new_in;
    cmd->in_redirs[cmd->in_redir_count].filepath = strdup(path); // copy path
    cmd->in_redir_count++;
}

// Appends an output redirection target to a Command (> or >>)
static void add_out_redir(Command *cmd, const char *path, bool append) {
    OutputRedir *new_out = realloc(cmd->out_redirs, (cmd->out_redir_count + 1) * sizeof(OutputRedir));
    if (new_out == NULL) {
        perror("realloc");
        return;
    }
    cmd->out_redirs = new_out;
    cmd->out_redirs[cmd->out_redir_count].filepath = strdup(path); // copy path
    cmd->out_redirs[cmd->out_redir_count].append = append;         // append flag
    cmd->out_redir_count++;
}

// Validates grammar and constructs executable Sequence data structure
bool parse_sequence(const Token *tokens, int count, Sequence *out_seq) {
    ParserState state = {tokens, count, 0};

    // 1. Verify input conforms to specified grammar
    if (!parse_LINE(&state) || state.tokens[state.idx].type != TOKEN_EOF) {
        fprintf(stderr, "cshell: invalid syntax\n"); // grammar rejection
        return false;
    }

    // 2. Return empty sequence if line contains only EOF
    if (tokens[0].type == TOKEN_EOF) {
        out_seq->pipelines = NULL;
        out_seq->pipeline_count = 0;
        return true;
    }

    // 3. Count pipelines separated by ';' or '&'
    int pipeline_count = 1;
    for (int i = 0; i < count; i++) {
        if (tokens[i].type == TOKEN_SEMI || tokens[i].type == TOKEN_AMP) {
            // only count if followed by non-EOF token
            if (i + 1 < count && tokens[i+1].type != TOKEN_EOF) {
                pipeline_count++;
            }
        }
    }

    out_seq->pipelines = malloc(pipeline_count * sizeof(Pipeline));
    if (out_seq->pipelines == NULL) {
        perror("malloc");
        return false;
    }
    out_seq->pipeline_count = pipeline_count;
    
    // 4. Construct pipelines and populate commands
    int cur_pipe_idx = 0;
    int cur_cmd_idx = 0;
    int cmd_count = 1;
    
    // Count commands in the first pipeline
    for (int i = 0; i < count; i++) {
        if (tokens[i].type == TOKEN_SEMI || tokens[i].type == TOKEN_AMP || tokens[i].type == TOKEN_EOF) {
            break;
        }
        if (tokens[i].type == TOKEN_PIPE) {
            cmd_count++; // increment pipe stage count
        }
    }
    
    out_seq->pipelines[0].cmds = malloc(cmd_count * sizeof(Command));
    out_seq->pipelines[0].cmd_count = cmd_count;
    out_seq->pipelines[0].is_background = false;
    for (int i = 0; i < cmd_count; i++) {
        init_command(&out_seq->pipelines[0].cmds[i]); // initialize command
    }

    TokenType last_op = TOKEN_EOF; // tracks pending redirect operator

    // Walk through tokens and build command structures
    for (int i = 0; i < count; i++) {
        TokenType t_type = tokens[i].type;

        if (t_type == TOKEN_EOF) {
            break; // reached end of tokens
        }
        
        // Handle pipeline boundaries (';' or '&')
        if (t_type == TOKEN_SEMI || t_type == TOKEN_AMP) {
            out_seq->pipelines[cur_pipe_idx].is_background = (t_type == TOKEN_AMP); // background flag
            
            if (i + 1 < count && tokens[i+1].type != TOKEN_EOF) {
                cur_pipe_idx++;       // advance to next pipeline
                cur_cmd_idx = 0;      // reset command index
                last_op = TOKEN_EOF;  // reset operator
                
                // Count commands for the next pipeline
                cmd_count = 1;
                for (int j = i + 1; j < count; j++) {
                    if (tokens[j].type == TOKEN_SEMI || tokens[j].type == TOKEN_AMP || tokens[j].type == TOKEN_EOF) {
                        break;
                    }
                    if (tokens[j].type == TOKEN_PIPE) {
                        cmd_count++;
                    }
                }
                
                // Allocate command array for next pipeline
                out_seq->pipelines[cur_pipe_idx].cmds = malloc(cmd_count * sizeof(Command));
                out_seq->pipelines[cur_pipe_idx].cmd_count = cmd_count;
                out_seq->pipelines[cur_pipe_idx].is_background = false;
                for (int j = 0; j < cmd_count; j++) {
                    init_command(&out_seq->pipelines[cur_pipe_idx].cmds[j]);
                }
            }
        } else if (t_type == TOKEN_PIPE) {
            cur_cmd_idx++;       // advance to next command in pipeline
            last_op = TOKEN_EOF; // clear operator
        } else if (t_type == TOKEN_LT || t_type == TOKEN_GT || t_type == TOKEN_GTGT) {
            last_op = t_type;    // record redirection operator
        } else if (t_type == TOKEN_WORD) {
            Command *cur_cmd = &out_seq->pipelines[cur_pipe_idx].cmds[cur_cmd_idx];
            // assign word as redirection target or command argument
            if (last_op == TOKEN_LT) {
                add_in_redir(cur_cmd, tokens[i].value); // input redirect
                last_op = TOKEN_EOF;
            } else if (last_op == TOKEN_GT) {
                add_out_redir(cur_cmd, tokens[i].value, false); // output truncate
                last_op = TOKEN_EOF;
            } else if (last_op == TOKEN_GTGT) {
                add_out_redir(cur_cmd, tokens[i].value, true);  // output append
                last_op = TOKEN_EOF;
            } else {
                add_arg(cur_cmd, tokens[i].value); // normal command argument
            }
        }
    }

    return true; // parsing and construction succeeded
}
