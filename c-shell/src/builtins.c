#include "builtins.h"
#include "hop.h"
#include "reveal.h"
#include "peek.h"
#include <string.h>

bool is_builtin(const char *cmd_name) {
    if (cmd_name == NULL) return false;
    return (strcmp(cmd_name, "hop") == 0 ||
            strcmp(cmd_name, "reveal") == 0 ||
            strcmp(cmd_name, "peek") == 0);
}

int exec_builtin(Command *cmd) {
    if (cmd == NULL || cmd->argc == 0) return 0;
    if (strcmp(cmd->args[0], "hop") == 0) {
        return builtin_hop(cmd);
    } else if (strcmp(cmd->args[0], "reveal") == 0) {
        return builtin_reveal(cmd);
    } else if (strcmp(cmd->args[0], "peek") == 0) {
        return builtin_peek(cmd);
    }
    return 1;
}
