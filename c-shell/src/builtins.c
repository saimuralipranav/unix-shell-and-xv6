#include "builtins.h"
#include "hop.h"
#include "reveal.h"
#include "peek.h"
#include "locate.h"
#include "activities.h"
#include "resume.h"
#include "ping.h"
#include "spy.h"
#include "snoop.h"
#include <string.h>

// Checks if a command name matches any shell intrinsic
bool is_builtin(const char *cmd_name) {
    if (cmd_name == NULL) return false; // null check
    // compare against all known builtins
    return (strcmp(cmd_name, "hop") == 0 ||
            strcmp(cmd_name, "reveal") == 0 ||
            strcmp(cmd_name, "peek") == 0 ||
            strcmp(cmd_name, "locate") == 0 ||
            strcmp(cmd_name, "activities") == 0 ||
            strcmp(cmd_name, "resume") == 0 ||
            strcmp(cmd_name, "ping") == 0 ||
            strcmp(cmd_name, "spy") == 0 ||
            strcmp(cmd_name, "snoop") == 0);
}

// Dispatches execution to the matching builtin handler
int exec_builtin(Command *cmd) {
    if (cmd == NULL || cmd->argc == 0) return 0; // sanity check
    if (strcmp(cmd->args[0], "hop") == 0) {
        return builtin_hop(cmd);        // execute hop
    } else if (strcmp(cmd->args[0], "reveal") == 0) {
        return builtin_reveal(cmd);     // execute reveal
    } else if (strcmp(cmd->args[0], "peek") == 0) {
        return builtin_peek(cmd);       // execute peek
    } else if (strcmp(cmd->args[0], "locate") == 0) {
        return builtin_locate(cmd);     // execute locate
    } else if (strcmp(cmd->args[0], "activities") == 0) {
        return builtin_activities(cmd); // execute activities
    } else if (strcmp(cmd->args[0], "resume") == 0) {
        return builtin_resume(cmd);     // execute resume
    } else if (strcmp(cmd->args[0], "ping") == 0) {
        return builtin_ping(cmd);       // execute ping
    } else if (strcmp(cmd->args[0], "spy") == 0) {
        return builtin_spy(cmd);        // execute spy
    } else if (strcmp(cmd->args[0], "snoop") == 0) {
        return builtin_snoop(cmd);      // execute snoop
    }
    return 1; // unknown builtin
}
