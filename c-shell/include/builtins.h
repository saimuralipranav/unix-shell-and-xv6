#ifndef BUILTINS_H
#define BUILTINS_H

#include "parser.h"

bool is_builtin(const char *cmd_name);
int exec_builtin(Command *cmd);

#endif // BUILTINS_H
