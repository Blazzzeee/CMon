#pragma once

#include <event2/event.h>

// Initialise async command execution. Must be called once after event_base_new().
// Registers the SIGCHLD handler that reaps children without blocking the event loop.
void run_cmd_argv_init(struct event_base *base);

// Tear down async command execution. Call before teardown_arena() on shutdown.
void run_cmd_argv_teardown(void);

// Execute a command asynchronously. Forks the child and returns immediately.
// exit_code is set to 0 (accepted) on success, -1 on fork/pipe failure.
// Output is captured and logged asynchronously by the event loop.
char *run_cmd_argv(const char *path, char *const argv[], int *exit_code);

// Specific runners
char *run_health(int *exit_code);
char *run_reboot(int *exit_code);
char *run_restart(int *exit_code);
char *run_git_pull(const char *branch, int *exit_code);
char *run_deploy_branch(const char *branch, int *exit_code);
char *run_teardown_branch(const char *branch, int *exit_code);
char *run_logs(int *exit_code);