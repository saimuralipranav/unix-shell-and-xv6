#include "jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

static TrackedJob jobs[MAX_TRACKED_JOBS]; // tracked jobs array
static int next_job_number = 1;          // monotonic session-wide job ID counter

// Clears all job slots in the tracking table
void init_jobs(void) {
    for (int i = 0; i < MAX_TRACKED_JOBS; i++) {
        jobs[i].job_number = 0;       // reset job number
        jobs[i].pgid = 0;             // reset process group id
        jobs[i].cmd_line[0] = '\0';   // clear command string
        jobs[i].procs = NULL;         // nullify process pointer
        jobs[i].proc_count = 0;       // reset process counter
        jobs[i].is_background = false;// reset background flag
        jobs[i].active = false;       // mark slot inactive
    }
    next_job_number = 1;              // initialize job sequence
}

// Allocates and initializes a new tracked job slot
TrackedJob *create_job(pid_t pgid, const char *cmd_line, bool is_background) {
    for (int i = 0; i < MAX_TRACKED_JOBS; i++) {
        // find first inactive slot
        if (!jobs[i].active) {
            jobs[i].job_number = next_job_number++; // assign monotonic ID
            jobs[i].pgid = pgid;                    // assign PGID
            if (cmd_line != NULL) {
                strncpy(jobs[i].cmd_line, cmd_line, sizeof(jobs[i].cmd_line) - 1); // copy command line
                jobs[i].cmd_line[sizeof(jobs[i].cmd_line) - 1] = '\0';             // null terminate
            } else {
                jobs[i].cmd_line[0] = '\0';
            }
            jobs[i].is_background = is_background; // store background flag
            jobs[i].active = true;                 // mark slot active
            jobs[i].procs = NULL;                  // initialize process array
            jobs[i].proc_count = 0;                // initialize count
            return &jobs[i];                       // return created job
        }
    }
    return NULL; // table full
}

// Appends a process record to a tracked job
void job_add_process(TrackedJob *job, pid_t pid, const char *cmd_name) {
    if (job == NULL) return; // sanity check
    // resize process array dynamically
    TrackedProcess *new_procs = realloc(job->procs, (job->proc_count + 1) * sizeof(TrackedProcess));
    if (new_procs == NULL) {
        perror("realloc"); // allocation failure
        return;
    }
    job->procs = new_procs; // update array pointer
    job->procs[job->proc_count].pid = pid; // record pid
    if (cmd_name != NULL) {
        // strip '%' prefix if present
        const char *name = (cmd_name[0] == '%') ? cmd_name + 1 : cmd_name;
        strncpy(job->procs[job->proc_count].cmd_name, name, sizeof(job->procs[job->proc_count].cmd_name) - 1);
        job->procs[job->proc_count].cmd_name[sizeof(job->procs[job->proc_count].cmd_name) - 1] = '\0';
    } else {
        job->procs[job->proc_count].cmd_name[0] = '\0';
    }
    job->procs[job->proc_count].state = PROC_RUNNING; // initial state running
    job->proc_count++;                                // increment count
}

// Retrieves a job pointer by slot index
TrackedJob *get_job_by_slot(int index) {
    if (index >= 0 && index < MAX_TRACKED_JOBS) {
        return &jobs[index]; // return slot pointer
    }
    return NULL; // out of bounds
}

// Finds an active job matching the given job number
TrackedJob *get_job_by_number(int job_number) {
    for (int i = 0; i < MAX_TRACKED_JOBS; i++) {
        // match job number on active entry
        if (jobs[i].active && jobs[i].job_number == job_number) {
            return &jobs[i]; // match found
        }
    }
    return NULL; // not found
}

// Finds an active job matching the given process group ID
TrackedJob *get_job_by_pgid(pid_t pgid) {
    for (int i = 0; i < MAX_TRACKED_JOBS; i++) {
        // match pgid on active entry
        if (jobs[i].active && jobs[i].pgid == pgid) {
            return &jobs[i]; // match found
        }
    }
    return NULL; // not found
}

// Finds the job and process index containing the specified PID
TrackedJob *get_job_by_pid(pid_t pid, int *out_proc_idx) {
    for (int i = 0; i < MAX_TRACKED_JOBS; i++) {
        if (jobs[i].active) {
            // search all processes in this job
            for (int p = 0; p < jobs[i].proc_count; p++) {
                if (jobs[i].procs[p].pid == pid) {
                    if (out_proc_idx != NULL) *out_proc_idx = p; // record index
                    return &jobs[i];                             // match found
                }
            }
        }
    }
    return NULL; // not found
}

// Structure to buffer background exit notifications
typedef struct {
    char cmd_name[256]; // command name
    pid_t pid;          // process id
    bool normal;        // true if normal exit, false if signaled
} ExitNotification;

static ExitNotification pending_exits[MAX_TRACKED_JOBS]; // notification buffer
static volatile int pending_exit_count = 0;              // pending count

// Reaps any terminated child processes without blocking
void reap_background_jobs(void) {
    int status;
    pid_t pid;
    // non-blocking wait on any child process
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        int proc_idx = -1;
        TrackedJob *job = get_job_by_pid(pid, &proc_idx); // locate matching job
        if (job != NULL && proc_idx >= 0) {
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                // buffer exit notification for lead process of background jobs
                if (job->is_background && proc_idx == 0) {
                    if (pending_exit_count < MAX_TRACKED_JOBS) {
                        strncpy(pending_exits[pending_exit_count].cmd_name, job->procs[0].cmd_name, sizeof(pending_exits[0].cmd_name) - 1);
                        pending_exits[pending_exit_count].cmd_name[sizeof(pending_exits[0].cmd_name) - 1] = '\0';
                        pending_exits[pending_exit_count].pid = pid;
                        pending_exits[pending_exit_count].normal = WIFEXITED(status);
                        pending_exit_count++;
                    }
                }
                job->procs[proc_idx].state = PROC_EXITED; // mark exited
            } else if (WIFSTOPPED(status)) {
                job->procs[proc_idx].state = PROC_STOPPED; // mark stopped
            } else if (WIFCONTINUED(status)) {
                job->procs[proc_idx].state = PROC_RUNNING; // mark running
            }
        }
    }
}

// Flushes buffered background exit notifications before drawing prompt
void flush_background_notifications(void) {
    reap_background_jobs(); // reap any pending children first
    for (int i = 0; i < pending_exit_count; i++) {
        // print normal vs abnormal exit message per specification
        if (pending_exits[i].normal) {
            printf("%s with pid %d exited normally\n", pending_exits[i].cmd_name, pending_exits[i].pid);
        } else {
            printf("%s with pid %d exited abnormally\n", pending_exits[i].cmd_name, pending_exits[i].pid);
        }
    }
    pending_exit_count = 0; // reset notification buffer
    fflush(stdout);         // flush output
}

// Synchronizes job states by inspecting /proc and reaping dead processes
void sync_job_states(void) {
    reap_background_jobs(); // non-blocking waitpid

    for (int i = 0; i < MAX_TRACKED_JOBS; i++) {
        if (!jobs[i].active) continue; // skip inactive

        // Check process states via /proc/<pid>/stat
        for (int p = 0; p < jobs[i].proc_count; p++) {
            if (jobs[i].procs[p].state == PROC_EXITED) continue;

            char stat_path[64];
            snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", jobs[i].procs[p].pid);
            FILE *f = fopen(stat_path, "r");
            if (f == NULL) {
                jobs[i].procs[p].state = PROC_EXITED; // process vanished
            } else {
                char line[512];
                if (fgets(line, sizeof(line), f) != NULL) {
                    char *rparen = strrchr(line, ')');
                    if (rparen && *(rparen + 1) == ' ') {
                        char st = *(rparen + 2); // state character
                        if (st == 'T' || st == 't') {
                            jobs[i].procs[p].state = PROC_STOPPED; // stopped
                        } else if (st == 'Z') {
                            jobs[i].procs[p].state = PROC_EXITED;  // zombie/exited
                        } else {
                            jobs[i].procs[p].state = PROC_RUNNING; // running
                        }
                    }
                }
                fclose(f); // close file
            }
        }

        // Compact active processes (remove PROC_EXITED)
        int active_count = 0;
        for (int p = 0; p < jobs[i].proc_count; p++) {
            if (jobs[i].procs[p].state != PROC_EXITED) {
                if (active_count != p) {
                    jobs[i].procs[active_count] = jobs[i].procs[p]; // shift forward
                }
                active_count++;
            }
        }
        jobs[i].proc_count = active_count; // update active count
        // if all processes in job exited, release job slot
        if (jobs[i].proc_count == 0) {
            jobs[i].active = false;
            if (jobs[i].procs != NULL) {
                free(jobs[i].procs); // free memory
                jobs[i].procs = NULL;
            }
        }
    }
}

// Checks if any tracked job currently has a stopped process
bool has_stopped_jobs(void) {
    sync_job_states(); // refresh statuses
    for (int i = 0; i < MAX_TRACKED_JOBS; i++) {
        if (jobs[i].active) {
            for (int p = 0; p < jobs[i].proc_count; p++) {
                if (jobs[i].procs[p].state == PROC_STOPPED) {
                    return true; // found stopped process
                }
            }
        }
    }
    return false; // no stopped processes
}

// Sends SIGHUP to all active job process groups upon shell exit
void sighup_all_jobs(void) {
    for (int i = 0; i < MAX_TRACKED_JOBS; i++) {
        if (jobs[i].active) {
            kill(-jobs[i].pgid, SIGHUP); // broadcast SIGHUP to process group
        }
    }
}
