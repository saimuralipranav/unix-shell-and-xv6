#ifndef JOBS_H
#define JOBS_H

#include "parser.h"
#include <sys/types.h>
#include <stdbool.h>

typedef enum {
    PROC_RUNNING,
    PROC_STOPPED,
    PROC_EXITED
} ProcessState;

typedef struct {
    pid_t pid;
    char cmd_name[256];
    ProcessState state;
} TrackedProcess;

typedef struct {
    int job_number;
    pid_t pgid;
    char cmd_line[1024];
    TrackedProcess *procs;
    int proc_count;
    bool is_background;
    bool active;
} TrackedJob;

#define MAX_TRACKED_JOBS 1024

void init_jobs(void);
TrackedJob *create_job(pid_t pgid, const char *cmd_line, bool is_background);
void job_add_process(TrackedJob *job, pid_t pid, const char *cmd_name);
TrackedJob *get_job_by_number(int job_number);
TrackedJob *get_job_by_pgid(pid_t pgid);
TrackedJob *get_job_by_pid(pid_t pid, int *out_proc_idx);
void sync_job_states(void);
void reap_background_jobs(void);
bool has_stopped_jobs(void);
void sighup_all_jobs(void);
TrackedJob *get_job_by_slot(int index);

int builtin_activities(Command *cmd);
int builtin_resume(Command *cmd);
int builtin_ping(Command *cmd);

#endif // JOBS_H
