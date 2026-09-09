#include "activities.h"
#include "jobs.h"
#include <stdio.h>

int builtin_activities(Command *cmd) {
    (void)cmd;

    sync_job_states();

    for (int i = 0; i < MAX_TRACKED_JOBS; i++) {
        TrackedJob *job = get_job_by_slot(i);
        if (job != NULL && job->active && job->proc_count > 0) {
            printf("[%d] pgid %d\n", job->job_number, job->pgid);
            for (int p = 0; p < job->proc_count; p++) {
                const char *state_str = (job->procs[p].state == PROC_STOPPED) ? "Stopped" : "Running";
                printf("  %d %s  %s\n", job->procs[p].pid, job->procs[p].cmd_name, state_str);
            }
        }
    }

    return 0;
}
