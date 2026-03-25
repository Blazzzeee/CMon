#pragma once

struct event_base;

// Register the libevent base used for async pipe-drain and SIGCHLD events.
// Must be called once before any run_* functions are used.
void commands_set_event_base(struct event_base *base);

// Execute a command asynchronously. Forks the child, registers libevent events
// to drain output and reap the process, then returns immediately.
// exit_code is set to 0 on successful fork; output is an empty arena string.
// Command output is drained by the event loop and discarded — it is not
// returned to the caller.
// Returns NULL on fork/pipe/alloc failure (exit_code left as -1).
char *run_cmd_argv(const char *path, char *const argv[], int *exit_code);

// Specific runners
char *run_health(int *exit_code);
char *run_reboot(int *exit_code);
char *run_restart(int *exit_code);
char *run_git_pull(const char *branch, int *exit_code);
char *run_deploy_branch(const char *branch, int *exit_code);
char *run_teardown_branch(const char *branch, int *exit_code);
char *run_logs(int *exit_code);
