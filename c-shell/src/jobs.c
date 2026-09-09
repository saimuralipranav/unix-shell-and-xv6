#include "jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

static TrackedJob jobs[MAX_TRACKED_JOBS];
static int next_job_number = 1;

void init_jobs(void) {
    for (int i = 0; i < MAX_TRACKED_JOBS; i++) {
        jobs[i].job_number = 0;
        jobs[i].pgid = 0;
        jobs[i].cmd_line[0] = '\0';
        jobs[i].procs = NULL;
        jobs[i].proc_count = 0;
        jobs[i].is_background = false;
        jobs[i].active = false;
    }
    next_job_number = 1;
}

TrackedJob *create_job(pid_t pgid, const char *cmd_line, bool is_background) {
    for (int i = 0; i < MAX_TRACKED_JOBS; i++) {
        if (!jobs[i].active) {
            jobs[i].job_number = next_job_number++;
            jobs[i].pgid = pgid;
            if (cmd_line != NULL) {
                strncpy(jobs[i].cmd_line, cmd_line, sizeof(jobs[i].cmd_line) - 1);
                jobs[i].cmd_line[sizeof(jobs[i].cmd_line) - 1] = '\0';
            } else {
                jobs[i].cmd_line[0] = '\0';
            }
            jobs[i].is_background = is_background;
            jobs[i].active = true;
            jobs[i].procs = NULL;
            jobs[i].proc_count = 0;
            return &jobs[i];
        }
    }
    return NULL;
}

void job_add_process(TrackedJob *job, pid_t pid, const char *cmd_name) {
    if (job == NULL) return;
    TrackedProcess *new_procs = realloc(job->procs, (job->proc_count + 1) * sizeof(TrackedProcess));
    if (new_procs == NULL) {
        perror("realloc");
        return;
    }
    job->procs = new_procs;
    job->procs[job->proc_count].pid = pid;
    if (cmd_name != NULL) {
        // Strip % if present
        const char *name = (cmd_name[0] == '%') ? cmd_name + 1 : cmd_name;
        strncpy(job->procs[job->proc_count].cmd_name, name, sizeof(job->procs[job->proc_count].cmd_name) - 1);
        job->procs[job->proc_count].cmd_name[sizeof(job->procs[job->proc_count].cmd_name) - 1] = '\0';
    } else {
        job->procs[job->proc_count].cmd_name[0] = '\0';
    }
    job->procs[job->proc_count].state = PROC_RUNNING;
    job->proc_count++;
}

TrackedJob *get_job_by_slot(int index) {
    if (index >= 0 && index < MAX_TRACKED_JOBS) {
        return &jobs[index];
    }
    return NULL;
}

TrackedJob *get_job_by_number(int job_number) {
    for (int i = 0; i < MAX_TRACKED_JOBS; i++) {
        if (jobs[i].active && jobs[i].job_number == job_number) {
            return &jobs[i];
        }
    }
    return NULL;
}

TrackedJob *get_job_by_pgid(pid_t pgid) {
    for (int i = 0; i < MAX_TRACKED_JOBS; i++) {
        if (jobs[i].active && jobs[i].pgid == pgid) {
            return &jobs[i];
        }
    }
    return NULL;
}

TrackedJob *get_job_by_pid(pid_t pid, int *out_proc_idx) {
    for (int i = 0; i < MAX_TRACKED_JOBS; i++) {
        if (jobs[i].active) {
            for (int p = 0; p < jobs[i].proc_count; p++) {
                if (jobs[i].procs[p].pid == pid) {
                    if (out_proc_idx != NULL) *out_proc_idx = p;
                    return &jobs[i];
                }
            }
        }
    }
    return NULL;
}

void reap_background_jobs(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        int proc_idx = -1;
        TrackedJob *job = get_job_by_pid(pid, &proc_idx);
        if (job != NULL && proc_idx >= 0) {
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                if (job->is_background && proc_idx == 0) {
                    if (WIFEXITED(status)) {
                        printf("%s with pid %d exited normally\n", job->procs[0].cmd_name, pid);
                    } else if (WIFSIGNALED(status)) {
                        printf("%s with pid %d exited abnormally\n", job->procs[0].cmd_name, pid);
                    }
                    fflush(stdout);
                }
                job->procs[proc_idx].state = PROC_EXITED;
            } else if (WIFSTOPPED(status)) {
                job->procs[proc_idx].state = PROC_STOPPED;
            } else if (WIFCONTINUED(status)) {
                job->procs[proc_idx].state = PROC_RUNNING;
            }
        }
    }
}

void sync_job_states(void) {
    reap_background_jobs();

    for (int i = 0; i < MAX_TRACKED_JOBS; i++) {
        if (!jobs[i].active) continue;

        for (int p = 0; p < jobs[i].proc_count; p++) {
            if (jobs[i].procs[p].state == PROC_EXITED) continue;

            char stat_path[64];
            snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", jobs[i].procs[p].pid);
            FILE *f = fopen(stat_path, "r");
            if (f == NULL) {
                jobs[i].procs[p].state = PROC_EXITED;
            } else {
                char line[512];
                if (fgets(line, sizeof(line), f) != NULL) {
                    char *rparen = strrchr(line, ')');
                    if (rparen && *(rparen + 1) == ' ') {
                        char st = *(rparen + 2);
                        if (st == 'T' || st == 't') {
                            jobs[i].procs[p].state = PROC_STOPPED;
                        } else if (st == 'Z') {
                            jobs[i].procs[p].state = PROC_EXITED;
                        } else {
                            jobs[i].procs[p].state = PROC_RUNNING;
                        }
                    }
                }
                fclose(f);
            }
        }

        // Compact active processes (remove PROC_EXITED)
        int active_count = 0;
        for (int p = 0; p < jobs[i].proc_count; p++) {
            if (jobs[i].procs[p].state != PROC_EXITED) {
                if (active_count != p) {
                    jobs[i].procs[active_count] = jobs[i].procs[p];
                }
                active_count++;
            }
        }
        jobs[i].proc_count = active_count;
        if (jobs[i].proc_count == 0) {
            jobs[i].active = false;
            if (jobs[i].procs != NULL) {
                free(jobs[i].procs);
                jobs[i].procs = NULL;
            }
        }
    }
}

bool has_stopped_jobs(void) {
    sync_job_states();
    for (int i = 0; i < MAX_TRACKED_JOBS; i++) {
        if (jobs[i].active) {
            for (int p = 0; p < jobs[i].proc_count; p++) {
                if (jobs[i].procs[p].state == PROC_STOPPED) {
                    return true;
                }
            }
        }
    }
    return false;
}

void sighup_all_jobs(void) {
    for (int i = 0; i < MAX_TRACKED_JOBS; i++) {
        if (jobs[i].active) {
            kill(-jobs[i].pgid, SIGHUP);
        }
    }
}
