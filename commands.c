#include "commands.h"
#include "arena.h"
#include "utils.h"
#include <errno.h>
#include <event2/event.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define SERVER_BINARY "target"

/* ── libevent integration ─────────────────────────────────────────────────── */

static struct event_base *g_base = NULL;
static struct event *g_sigchld_ev = NULL;

/* Reap all finished children without blocking. */
static void sigchld_cb(evutil_socket_t sig, short events, void *arg) {
    (void)sig;
    (void)events;
    (void)arg;
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
}

void commands_set_event_base(struct event_base *base) {
    g_base = base;
    g_sigchld_ev = evsignal_new(base, SIGCHLD, sigchld_cb, NULL);
    if (g_sigchld_ev) {
        event_add(g_sigchld_ev, NULL);
    } else {
        log_error("evsignal_new(SIGCHLD) failed; children may become zombies");
    }
}

/* ── Async pipe drain ─────────────────────────────────────────────────────── */

typedef struct {
    struct event *ev;
    int fd;
} pipe_ctx;

/* Called by libevent when the child's stdout/stderr pipe is readable.
 * Drains available bytes and tears down the event + fd on EOF or error. */
static void pipe_drain_cb(evutil_socket_t fd, short what, void *arg) {
    (void)what;
    pipe_ctx *ctx = (pipe_ctx *)arg;
    char buf[512];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        ;
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        event_del(ctx->ev);
        event_free(ctx->ev);
        close(fd);
        free(ctx);
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */

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
        /* child: wire stdout/stderr into the pipe then exec */
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(path, argv);
        fprintf(stderr, "execvp failed: %s\n", path);
        _exit(127);
    }

    /* parent */
    close(pipefd[1]);

    /* Make the read-end non-blocking so the event loop never stalls. */
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags == -1 || fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK) == -1) {
        log_error("fcntl O_NONBLOCK failed on pipe read-end");
        close(pipefd[0]);
        return NULL;
    }

    if (g_base) {
        /* Register a persistent read event to drain the pipe asynchronously. */
        pipe_ctx *ctx = malloc(sizeof(pipe_ctx));
        if (ctx) {
            ctx->fd = pipefd[0];
            ctx->ev = event_new(g_base, pipefd[0], EV_READ | EV_PERSIST, pipe_drain_cb, ctx);
            if (ctx->ev) {
                event_add(ctx->ev, NULL);
            } else {
                free(ctx);
                close(pipefd[0]);
            }
        } else {
            close(pipefd[0]);
        }
    } else {
        /* No event base available: close the fd; child exits on SIGPIPE. */
        close(pipefd[0]);
    }

    /* Return immediately — the job is now running in the background. */
    char *result = allocate(1);
    if (!result) {
        log_error("allocate failed in run_cmd_argv");
        return NULL;
    }
    result[0] = '\0';
    *exit_code_out = 0;
    return result;
}

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
