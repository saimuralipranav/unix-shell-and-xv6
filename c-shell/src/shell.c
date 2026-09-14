#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

char shell_home_dir[MAX_PATH] = "";
char previous_cwd[MAX_PATH] = "";
char username[256] = "unknown";
char hostname[256] = "unknown";

// Initializes shell environment variables: home directory, username, and hostname
void init_shell_state(void) {
    // 1. Get initial working directory as shell home
    if (getcwd(shell_home_dir, sizeof(shell_home_dir)) == NULL) {
        perror("getcwd");
        exit(EXIT_FAILURE);
    }
    
    // 2. Query username via getpwuid with fallback to USER env
    uid_t uid = getuid();                // get process uid
    struct passwd *pw = getpwuid(uid);   // query passwd entry
    if (pw != NULL && pw->pw_name != NULL) {
        strncpy(username, pw->pw_name, sizeof(username) - 1); // copy username
    } else {
        const char *user_env = getenv("USER"); // check USER environment variable
        if (user_env != NULL) {
            strncpy(username, user_env, sizeof(username) - 1); // fallback copy
        }
    }
    username[sizeof(username) - 1] = '\0'; // null-terminate
    
    // 3. Query system hostname with fallback to HOSTNAME env
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        const char *host_env = getenv("HOSTNAME"); // check HOSTNAME environment variable
        if (host_env != NULL) {
            strncpy(hostname, host_env, sizeof(hostname) - 1); // fallback copy
        }
    }
    hostname[sizeof(hostname) - 1] = '\0'; // null-terminate
}

// Prints the dynamic prompt: <username@hostname:path>
void print_prompt(void) {
    char cwd[MAX_PATH];
    // get current working directory
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd");
        return;
    }
    
    size_t home_len = strlen(shell_home_dir); // length of home dir
    char display_path[MAX_PATH * 2];          // formatted path buffer
    
    // Check if CWD is under the shell home directory
    if (strncmp(cwd, shell_home_dir, home_len) == 0 &&
        (cwd[home_len] == '\0' || cwd[home_len] == '/')) {
        // replace home prefix with '~'
        snprintf(display_path, sizeof(display_path), "~%s", cwd + home_len);
    } else {
        // show absolute path as is
        strncpy(display_path, cwd, sizeof(display_path) - 1);
        display_path[sizeof(display_path) - 1] = '\0'; // null-terminate
    }
    
    // Display prompt and flush stdout immediately
    printf("<%s@%s:%s> ", username, hostname, display_path);
    fflush(stdout);
}
