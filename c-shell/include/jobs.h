#ifndef JOBS_H
#define JOBS_H

#include "parser.h"
#include <sys/types.h>
#include <stdbool.h>

// Execution states for tracked child processes
typedef enum {
    PROC_RUNNING, // Process is actively executing
    PROC_STOPPED, // Process is suspended via signal (e.g., SIGTSTP)
    PROC_EXITED   // Process has terminated
} ProcessState;

// State tracking for an individual child process in a pipeline
typedef struct {
    pid_t pid;           // Process ID
    char cmd_name[256];  // Command name/string
    ProcessState state;  // Current run state
} TrackedProcess;

// Job structure grouping pipelines and background tasks
typedef struct {
    int job_number;         // User-visible sequential 1-based job ID
    pid_t pgid;             // Process group ID
    char cmd_line[1024];    // Full command line representation
    TrackedProcess *procs;  // Array of tracked child processes
    int proc_count;         // Number of processes in pipeline
    bool is_background;     // True if launched in background (&)
    bool active;            // True if job slot is in active use
} TrackedJob;

// Capacity limit for active tracked jobs
#define MAX_TRACKED_JOBS 1024

// Job table lifecycle and manipulation functions
void init_jobs(void);                                                           // Initialize jobs table
TrackedJob *create_job(pid_t pgid, const char *cmd_line, bool is_background); // Allocate new job slot
void job_add_process(TrackedJob *job, pid_t pid, const char *cmd_name);         // Append process to job
TrackedJob *get_job_by_number(int job_number);                                 // Lookup by 1-based job ID
TrackedJob *get_job_by_pgid(pid_t pgid);                                       // Lookup by process group ID
TrackedJob *get_job_by_pid(pid_t pid, int *out_proc_idx);                       // Lookup by individual PID
void sync_job_states(void);                                                    // Poll status of all processes
void reap_background_jobs(void);                                               // Non-blocking wait for bg children
void flush_background_notifications(void);                                     // Print termination messages
bool has_stopped_jobs(void);                                                   // Check if any stopped jobs exist
void sighup_all_jobs(void);                                                    // Forward SIGHUP on shell exit
TrackedJob *get_job_by_slot(int index);                                        // Direct slot access for iteration

// Builtin command dispatch prototypes
int builtin_activities(Command *cmd);                                          // activities builtin
int builtin_resume(Command *cmd);                                              // resume builtin
int builtin_ping(Command *cmd);                                                // ping builtin

#endif // JOBS_H
