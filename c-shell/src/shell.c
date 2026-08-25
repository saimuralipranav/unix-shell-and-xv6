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

void init_shell_state(void) {
    // 1. Get home directory (directory shell starts in)
    if (getcwd(shell_home_dir, sizeof(shell_home_dir)) == NULL) {
        perror("getcwd");
        exit(EXIT_FAILURE);
    }
    
    // 2. Get username using POSIX getpwuid or USER environment fallback
    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    if (pw != NULL && pw->pw_name != NULL) {
        strncpy(username, pw->pw_name, sizeof(username) - 1);
    } else {
        const char *user_env = getenv("USER");
        if (user_env != NULL) {
            strncpy(username, user_env, sizeof(username) - 1);
        }
    }
    username[sizeof(username) - 1] = '\0';
    
    // 3. Get hostname using POSIX gethostname
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        const char *host_env = getenv("HOSTNAME");
        if (host_env != NULL) {
            strncpy(hostname, host_env, sizeof(hostname) - 1);
        }
    }
    hostname[sizeof(hostname) - 1] = '\0';
}

void print_prompt(void) {
    char cwd[MAX_PATH];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd");
        return;
    }
    
    size_t home_len = strlen(shell_home_dir);
    char display_path[MAX_PATH * 2];
    
    if (strncmp(cwd, shell_home_dir, home_len) == 0 &&
        (cwd[home_len] == '\0' || cwd[home_len] == '/')) {
        snprintf(display_path, sizeof(display_path), "~%s", cwd + home_len);
    } else {
        strncpy(display_path, cwd, sizeof(display_path) - 1);
        display_path[sizeof(display_path) - 1] = '\0';
    }
    
    printf("<%s@%s:%s> ", username, hostname, display_path);
    fflush(stdout);
}
