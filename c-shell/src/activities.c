#include "activities.h"
#include "jobs.h"
#include <stdio.h>
#include <stdlib.h>

// Job comparison callback: sorts active jobs by ascending job number
static int compare_jobs(const void *a, const void *b) {
    const TrackedJob *ja = *(const TrackedJob **)a; // cast first job pointer
    const TrackedJob *jb = *(const TrackedJob **)b; // cast second job pointer
    return ja->job_number - jb->job_number;         // compare job numbers
}

// Builtin 'activities': lists all currently tracked active jobs
int builtin_activities(Command *cmd) {
    (void)cmd; // unused parameter

    // Synchronize states of child processes
    sync_job_states();

    TrackedJob *active_jobs[MAX_TRACKED_JOBS]; // array to hold active jobs
    int count = 0;                             // active job counter

    // Collect all active tracked jobs
    for (int i = 0; i < MAX_TRACKED_JOBS; i++) {
        TrackedJob *job = get_job_by_slot(i); // get job slot
        // check if job is active and has processes
        if (job != NULL && job->active && job->proc_count > 0) {
            active_jobs[count++] = job; // record active job
        }
    }

    // Sort active jobs in ascending order of launch
    if (count > 0) {
        qsort(active_jobs, count, sizeof(TrackedJob *), compare_jobs);
    }

    // Print each active job group and process list
    for (int i = 0; i < count; i++) {
        TrackedJob *job = active_jobs[i]; // current job
        printf("[%d] pgid %d\n", job->job_number, job->pgid); // print group header
        for (int p = 0; p < job->proc_count; p++) {
            // format process state string
            const char *state_str = (job->procs[p].state == PROC_STOPPED) ? "Stopped" : "Running";
            // print process details with 2-space indentation
            printf("  %d %s  %s\n", job->procs[p].pid, job->procs[p].cmd_name, state_str);
        }
    }

    return 0; // success
}
