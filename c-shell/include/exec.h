#ifndef EXEC_H
#define EXEC_H

#include "parser.h"
#include <sys/types.h>

// Executes a parsed sequence of pipelines
// Handles both sequential (;) and background (&) execution modes
int execute_sequence(Sequence *seq);

// Executes a single pipeline. Returns PID of the background process group leader
// (or the first command's PID), or status if synchronous.
// Returns -2 if a command fails to start (for sequence abort).
int execute_pipeline(Pipeline *pipeline, bool *failed_to_start);

// Job control
void register_background_job(pid_t pid, const char *cmd_name);
void check_background_jobs(void);

#endif // EXEC_H
