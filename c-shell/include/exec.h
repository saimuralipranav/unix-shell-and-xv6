#ifndef EXEC_H
#define EXEC_H

#include "parser.h"
#include <stdbool.h>

// Executes a parsed sequence of pipelines
// Handles sequential execution (;)
int execute_sequence(Sequence *seq);

// Executes a single pipeline. Returns status.
// Returns -2 if a command fails to start (for sequence abort).
int execute_pipeline(Pipeline *pipeline, bool *failed_to_start);

#endif // EXEC_H
