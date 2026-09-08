#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const Token *tokens;
    int count;
    int idx;
} ParserState;

static inline const Token *peek_tok(ParserState *state) {
    if (state->idx >= state->count) {
        return NULL;
    }
    return &state->tokens[state->idx];
}

static inline const Token *advance_tok(ParserState *state) {
    if (state->idx >= state->count) {
        return NULL;
    }
    return &state->tokens[state->idx++];
}

static bool parse_LINE(ParserState *state);
static bool parse_ARG(ParserState *state);
static bool parse_CMD(ParserState *state);
static bool parse_TGT(ParserState *state);
static bool parse_BG(ParserState *state);

static bool parse_LINE(ParserState *state) {
    const Token *t = peek_tok(state);
    if (t == NULL) return false;

    if (t->type == TOKEN_EOF) {
        return true;
    }
    if (t->type == TOKEN_WORD) {
        advance_tok(state);
        return parse_ARG(state);
    }
    return false;
}

static bool parse_ARG(ParserState *state) {
    const Token *t = peek_tok(state);
    if (t == NULL) return false;

    if (t->type == TOKEN_EOF) {
        return true;
    }
    if (t->type == TOKEN_WORD) {
        advance_tok(state);
        return parse_ARG(state);
    }
    if (t->type == TOKEN_LT) {
        advance_tok(state);
        return parse_TGT(state);
    }
    if (t->type == TOKEN_GT || t->type == TOKEN_GTGT) {
        advance_tok(state);
        return parse_TGT(state);
    }
    if (t->type == TOKEN_PIPE) {
        advance_tok(state);
        return parse_CMD(state);
    }
    if (t->type == TOKEN_SEMI) {
        advance_tok(state);
        return parse_CMD(state);
    }
    if (t->type == TOKEN_AMP) {
        advance_tok(state);
        return parse_BG(state);
    }
    return false;
}

static bool parse_CMD(ParserState *state) {
    const Token *t = peek_tok(state);
    if (t == NULL) return false;

    if (t->type == TOKEN_WORD) {
        advance_tok(state);
        return parse_ARG(state);
    }
    return false;
}

static bool parse_TGT(ParserState *state) {
    const Token *t = peek_tok(state);
    if (t == NULL) return false;

    if (t->type == TOKEN_WORD) {
        advance_tok(state);
        return parse_ARG(state);
    }
    return false;
}

static bool parse_BG(ParserState *state) {
    const Token *t = peek_tok(state);
    if (t == NULL) return false;

    if (t->type == TOKEN_EOF) {
        return true;
    }
    if (t->type == TOKEN_WORD) {
        advance_tok(state);
        return parse_ARG(state);
    }
    return false;
}

static void free_pipeline(Pipeline *pipeline) {
    if (pipeline == NULL || pipeline->cmds == NULL) return;
    for (int i = 0; i < pipeline->cmd_count; i++) {
        Command *cmd = &pipeline->cmds[i];
        if (cmd->args != NULL) {
            for (int j = 0; j < cmd->argc; j++) {
                free(cmd->args[j]);
            }
            free(cmd->args);
        }
        if (cmd->in_redirs != NULL) {
            for (int j = 0; j < cmd->in_redir_count; j++) {
                free(cmd->in_redirs[j].filepath);
            }
            free(cmd->in_redirs);
        }
        if (cmd->out_redirs != NULL) {
            for (int j = 0; j < cmd->out_redir_count; j++) {
                free(cmd->out_redirs[j].filepath);
            }
            free(cmd->out_redirs);
        }
    }
    free(pipeline->cmds);
    pipeline->cmds = NULL;
    pipeline->cmd_count = 0;
}

void free_sequence(Sequence *seq) {
    if (seq == NULL || seq->pipelines == NULL) return;
    for (int i = 0; i < seq->pipeline_count; i++) {
        free_pipeline(&seq->pipelines[i]);
    }
    free(seq->pipelines);
    seq->pipelines = NULL;
    seq->pipeline_count = 0;
}

static void init_command(Command *cmd) {
    cmd->args = NULL;
    cmd->argc = 0;
    cmd->in_redirs = NULL;
    cmd->in_redir_count = 0;
    cmd->out_redirs = NULL;
    cmd->out_redir_count = 0;
}

static void add_arg(Command *cmd, const char *arg) {
    char **new_args = realloc(cmd->args, (cmd->argc + 2) * sizeof(char *));
    if (new_args == NULL) {
        perror("realloc");
        return;
    }
    cmd->args = new_args;
    cmd->args[cmd->argc] = strdup(arg);
    cmd->argc++;
    cmd->args[cmd->argc] = NULL;
}

static void add_in_redir(Command *cmd, const char *path) {
    InputRedir *new_in = realloc(cmd->in_redirs, (cmd->in_redir_count + 1) * sizeof(InputRedir));
    if (new_in == NULL) {
        perror("realloc");
        return;
    }
    cmd->in_redirs = new_in;
    cmd->in_redirs[cmd->in_redir_count].filepath = strdup(path);
    cmd->in_redir_count++;
}

static void add_out_redir(Command *cmd, const char *path, bool append) {
    OutputRedir *new_out = realloc(cmd->out_redirs, (cmd->out_redir_count + 1) * sizeof(OutputRedir));
    if (new_out == NULL) {
        perror("realloc");
        return;
    }
    cmd->out_redirs = new_out;
    cmd->out_redirs[cmd->out_redir_count].filepath = strdup(path);
    cmd->out_redirs[cmd->out_redir_count].append = append;
    cmd->out_redir_count++;
}

bool parse_sequence(const Token *tokens, int count, Sequence *out_seq) {
    ParserState state = {tokens, count, 0};

    // 1. Verify grammar syntax
    if (!parse_LINE(&state) || state.tokens[state.idx].type != TOKEN_EOF) {
        fprintf(stderr, "cshell: invalid syntax\n");
        return false;
    }

    // 2. If it's an empty line (just TOKEN_EOF), return true with 0 pipelines
    if (tokens[0].type == TOKEN_EOF) {
        out_seq->pipelines = NULL;
        out_seq->pipeline_count = 0;
        return true;
    }

    // 3. Count pipelines (separated by ; or &)
    int pipeline_count = 1;
    for (int i = 0; i < count; i++) {
        if (tokens[i].type == TOKEN_SEMI || tokens[i].type == TOKEN_AMP) {
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
    
    // Iterate over tokens again and construct the sequence
    int cur_pipe_idx = 0;
    int cur_cmd_idx = 0;
    int cmd_count = 1;
    
    // First, count commands in the first pipeline
    for (int i = 0; i < count; i++) {
        if (tokens[i].type == TOKEN_SEMI || tokens[i].type == TOKEN_AMP || tokens[i].type == TOKEN_EOF) {
            break;
        }
        if (tokens[i].type == TOKEN_PIPE) {
            cmd_count++;
        }
    }
    
    out_seq->pipelines[0].cmds = malloc(cmd_count * sizeof(Command));
    out_seq->pipelines[0].cmd_count = cmd_count;
    out_seq->pipelines[0].is_background = false;
    for (int i = 0; i < cmd_count; i++) {
        init_command(&out_seq->pipelines[0].cmds[i]);
    }

    TokenType last_op = TOKEN_EOF;

    for (int i = 0; i < count; i++) {
        TokenType t_type = tokens[i].type;

        if (t_type == TOKEN_EOF) {
            break;
        }
        
        if (t_type == TOKEN_SEMI || t_type == TOKEN_AMP) {
            out_seq->pipelines[cur_pipe_idx].is_background = (t_type == TOKEN_AMP);
            
            if (i + 1 < count && tokens[i+1].type != TOKEN_EOF) {
                cur_pipe_idx++;
                cur_cmd_idx = 0;
                last_op = TOKEN_EOF;
                
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
                
                out_seq->pipelines[cur_pipe_idx].cmds = malloc(cmd_count * sizeof(Command));
                out_seq->pipelines[cur_pipe_idx].cmd_count = cmd_count;
                out_seq->pipelines[cur_pipe_idx].is_background = false;
                for (int j = 0; j < cmd_count; j++) {
                    init_command(&out_seq->pipelines[cur_pipe_idx].cmds[j]);
                }
            }
        } else if (t_type == TOKEN_PIPE) {
            cur_cmd_idx++;
            last_op = TOKEN_EOF;
        } else if (t_type == TOKEN_LT || t_type == TOKEN_GT || t_type == TOKEN_GTGT) {
            last_op = t_type;
        } else if (t_type == TOKEN_WORD) {
            Command *cur_cmd = &out_seq->pipelines[cur_pipe_idx].cmds[cur_cmd_idx];
            if (last_op == TOKEN_LT) {
                add_in_redir(cur_cmd, tokens[i].value);
                last_op = TOKEN_EOF;
            } else if (last_op == TOKEN_GT) {
                add_out_redir(cur_cmd, tokens[i].value, false);
                last_op = TOKEN_EOF;
            } else if (last_op == TOKEN_GTGT) {
                add_out_redir(cur_cmd, tokens[i].value, true);
                last_op = TOKEN_EOF;
            } else {
                add_arg(cur_cmd, tokens[i].value);
            }
        }
    }

    return true;
}
