#include "commands.h"
#include "arena.h"
#include "utils.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <event2/event.h>

#define SERVER_BINARY "target"
#define OUTPUT_BUF_INIT 512

// -- Job Context --

// Tracks one in-flight command from fork() until the child exits.
// Arena-allocated before fork(), freed back to arena in sigchld_cb().
// `next` links all live jobs into a singly-linked list for O(n) PID lookup.
typedef struct cmd_job {
    pid_t           pid;
    int             pipe_fd;
    struct event   *pipe_ev;
    struct cmd_job *next;
    struct timeval  start;
    char           *output;
    size_t          capacity;
    size_t          total;
    char            path[256];
} cmd_job_t;

// -- Module State --

static struct event_base *g_base    = NULL;
static struct event      *g_sigchld = NULL;
static cmd_job_t         *g_jobs    = NULL;

// -- Internal Helpers --

static void job_append(cmd_job_t *job, const char *buf, size_t n) {
    // Grow the arena buffer if this read would overflow it.
    // Keep 1 byte spare so output[total] = '\0' is always safe.
    if (job->total + n + 1 > job->capacity) {
        size_t new_capacity = job->capacity * 2;
        char *new_output = allocate(new_capacity);
        if (!new_output) {
            log_error("allocate failed during output growth");
            return;
        }
        memcpy(new_output, job->output, job->total);
        deallocate(job->output);
        job->output = new_output;
        job->capacity = new_capacity;
    }
    memcpy(job->output + job->total, buf, n);
    job->total += n;
}

static void job_free(cmd_job_t *job, int exit_code) {
    job->output[job->total] = '\0';

    struct timeval end;
    gettimeofday(&end, NULL);
    double duration = (end.tv_sec - job->start.tv_sec) +
                      (end.tv_usec - job->start.tv_usec) / 1000000.0;

    log_exec(job->path, exit_code, duration);

    deallocate(job->output);
    deallocate(job);
}

// -- Async Callbacks --

// Drain the child's pipe into the job output buffer without blocking.
// Called by libevent whenever data is available on the pipe read-end.
static void pipe_read_cb(evutil_socket_t fd, short what, void *arg) {
    (void)what;
    cmd_job_t *job = arg;
    char temp_buf[OUTPUT_BUF_INIT];
    ssize_t n;

    while ((n = read(fd, temp_buf, sizeof(temp_buf))) > 0)
        job_append(job, temp_buf, n);

    // n == 0 means EOF (child closed its end). Real errors also close the event.
    if (n == 0 || (n == -1 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        event_del(job->pipe_ev);
        event_free(job->pipe_ev);
        job->pipe_ev = NULL;
        close(job->pipe_fd);
        job->pipe_fd = -1;
    }
}

// Reap all exited children without blocking.
// Called by libevent on SIGCHLD. Loops because the kernel may coalesce
// multiple signals into one delivery.
static void sigchld_cb(evutil_socket_t sig, short what, void *arg) {
    (void)sig;
    (void)what;
    (void)arg;

    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // Find and unlink the matching job
        cmd_job_t *prev = NULL;
        cmd_job_t *job  = g_jobs;
        while (job && job->pid != pid) {
            prev = job;
            job  = job->next;
        }

        if (!job)
            continue; // not our child

        if (prev)
            prev->next = job->next;
        else
            g_jobs = job->next;

        // Tear down pipe event if still active
        if (job->pipe_ev) {
            event_del(job->pipe_ev);
            event_free(job->pipe_ev);
            job->pipe_ev = NULL;
        }

        // Final drain — child is dead so the kernel buffer is fixed, won't block
        if (job->pipe_fd >= 0) {
            char temp_buf[OUTPUT_BUF_INIT];
            ssize_t n;
            while ((n = read(job->pipe_fd, temp_buf, sizeof(temp_buf))) > 0)
                job_append(job, temp_buf, n);
            close(job->pipe_fd);
            job->pipe_fd = -1;
        }

        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        job_free(job, exit_code);
    }
}

// -- Lifecycle --

void run_cmd_argv_init(struct event_base *base) {
    g_base = base;
    g_sigchld = evsignal_new(base, SIGCHLD, sigchld_cb, NULL);
    if (!g_sigchld) {
        log_error("evsignal_new(SIGCHLD) failed");
        return;
    }
    event_add(g_sigchld, NULL);
}

void run_cmd_argv_teardown(void) {
    if (g_sigchld) {
        event_del(g_sigchld);
        event_free(g_sigchld);
        g_sigchld = NULL;
    }
    g_jobs = NULL;
}

// -- Core Executor --

// Previously this function blocked the entire event loop by calling:
//   waitpid(pid, NULL, 0)  — line 56  (error path after allocate failure)
//   waitpid(pid, &status, 0) — line 108 (main reap after pipe drain)
//
// Now it forks, sets the pipe read-end O_NONBLOCK, arena-allocates a
// job context, registers a libevent pipe event, and returns immediately.
// The SIGCHLD handler above reaps the child and logs the result.
char *run_cmd_argv(const char *path, char *const argv[], int *exit_code_out) {
    *exit_code_out = -1;

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        log_error("pipe failed");
        return NULL;
    }

    pid_t pid = fork();
    if (pid == -1) {
        log_error("fork failed");
        close(pipefd[0]);
        close(pipefd[1]);
        return NULL;
    }

    if (pid == 0) {
        // child
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(path, argv);
        fprintf(stderr, "execvp failed: %s\n", path);
        _exit(127);
    }

    // parent
    close(pipefd[1]);

    // Make the read-end non-blocking so pipe_read_cb never stalls the event loop
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags == -1 || fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK) == -1) {
        log_error("fcntl O_NONBLOCK failed");
        close(pipefd[0]);
        return NULL;
    }

    // Allocate job context from arena
    cmd_job_t *job = allocate(sizeof(cmd_job_t));
    if (!job) {
        log_error("allocate failed for job context");
        close(pipefd[0]);
        return NULL;
    }
    memset(job, 0, sizeof(cmd_job_t));

    job->output = allocate(OUTPUT_BUF_INIT);
    if (!job->output) {
        log_error("allocate failed for output buffer");
        deallocate(job);
        close(pipefd[0]);
        return NULL;
    }

    job->pid      = pid;
    job->pipe_fd  = pipefd[0];
    job->capacity = OUTPUT_BUF_INIT;
    job->total    = 0;
    gettimeofday(&job->start, NULL);
    strncpy(job->path, path, sizeof(job->path) - 1);

    job->pipe_ev = event_new(g_base, pipefd[0], EV_READ | EV_PERSIST, pipe_read_cb, job);
    if (!job->pipe_ev) {
        log_error("event_new for pipe failed");
        deallocate(job->output);
        deallocate(job);
        close(pipefd[0]);
        return NULL;
    }
    event_add(job->pipe_ev, NULL);

    // Prepend to live-job list so sigchld_cb can find it by PID
    job->next = g_jobs;
    g_jobs    = job;

    *exit_code_out = 0; // accepted — real exit code is logged asynchronously
    return NULL;
}

// -- Command Runners --

char *run_health(int *exit_code) {
    char *argv[] = {"uptime", NULL};
    return run_cmd_argv("uptime", argv, exit_code);
}

char *run_reboot(int *exit_code) {
    char *argv[] = {"reboot", NULL};
    return run_cmd_argv("reboot", argv, exit_code);
}

char *run_restart(int *exit_code) {
    char *argv[] = {"pkill", SERVER_BINARY, NULL};
    return run_cmd_argv("pkill", argv, exit_code);
}

char *run_git_pull(const char *branch, int *exit_code) {
    if (!branch)
        branch = "main";
    char *argv[] = {"git", "pull", "origin", (char *)branch, NULL};
    return run_cmd_argv("git", argv, exit_code);
}

char *run_deploy_branch(const char *branch, int *exit_code) {
    if (!branch)
        branch = "main";
    char *argv[] = {"./deploy.sh", (char *)branch, NULL};
    return run_cmd_argv("./deploy.sh", argv, exit_code);
}

char *run_teardown_branch(const char *branch, int *exit_code) {
    if (!branch)
        branch = "main";
    char *argv[] = {"./teardown.sh", (char *)branch, NULL};
    return run_cmd_argv("./teardown.sh", argv, exit_code);
}

char *run_logs(int *exit_code) {
    char *argv[] = {"journalctl", "-t", "cmon", "-n", "50", "--no-pager", NULL};
    return run_cmd_argv("journalctl", argv, exit_code);
}