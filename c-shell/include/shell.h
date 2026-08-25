#ifndef SHELL_H
#define SHELL_H

#include <stdbool.h>
#include <stddef.h>

#define MAX_PATH 4096
#define MAX_INPUT 1024

// Global state variables
extern char shell_home_dir[MAX_PATH];
extern char previous_cwd[MAX_PATH];
extern char username[256];
extern char hostname[256];

// Initialize shell state (username, hostname, home directory)
void init_shell_state(void);

// Print the shell prompt: <username@hostname:currentpath>
void print_prompt(void);

#endif // SHELL_H
