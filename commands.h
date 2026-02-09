#pragma once

// Execute a command, return output (allocated in arena), set exit_code.
// Returns NULL on execution failure (exit_code set to 127 or similar).
char *run_cmd_argv(const char *path, char *const argv[], int *exit_code);

// Specific runners
char *run_health(int *exit_code);
char *run_reboot(int *exit_code);
char *run_restart(int *exit_code);
char *run_git_pull(const char *branch, int *exit_code);
char *run_deploy_branch(const char *branch, int *exit_code);
char *run_teardown_branch(const char *branch, int *exit_code);
char *run_logs(int *exit_code);
