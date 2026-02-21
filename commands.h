#pragma once

#include <event2/event.h>

/*
 * Initialise the async command layer.
 * Must be called once after event_base_new() in main(), before any
 * command function is invoked.
 *
 * NEW: registers the shared SIGCHLD handler that replaces the two
 * blocking waitpid() calls that previously lived in run_cmd_argv().
 */
void run_cmd_argv_init(struct event_base *base);

/*
 * Tear down the async command layer.
 * Call this in main()'s shutdown path, after event_base_loopbreak()
 * and before event_base_free().
 */
void run_cmd_argv_teardown(void);

/*
 * Execute a command asynchronously.
 *
 * CHANGED: no longer blocks.  Forks the child, registers a non-blocking
 * pipe event and a SIGCHLD handler, then returns NULL immediately.
 * *exit_code_out is set to 0 ("accepted").  Actual exit-code and output
 * are processed asynchronously by the event loop.
 *
 * Callers should send an immediate HTTP 202 Accepted response.
 */
char *run_cmd_argv(const char *path, char *const argv[], int *exit_code);

/* Specific runners — public API unchanged */
char *run_health(int *exit_code);
char *run_reboot(int *exit_code);
char *run_restart(int *exit_code);
char *run_git_pull(const char *branch, int *exit_code);
char *run_deploy_branch(const char *branch, int *exit_code);
char *run_teardown_branch(const char *branch, int *exit_code);
char *run_logs(int *exit_code);
