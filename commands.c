#include "commands.h"
#include "arena.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#define SERVER_BINARY "target"

char *run_cmd_argv(const char *path, char *const argv[], int *exit_code_out) {
    struct timeval start, end;
    gettimeofday(&start, NULL);

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

    // Dynamic allocation using arena
    size_t capacity = 512;
    size_t total = 0;
    char *output = allocate(capacity);

    if (!output) {
        log_error("allocate failed in run_cmd_argv");
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        return NULL;
    }

    char temp_buf[512];
    ssize_t n;
    while ((n = read(pipefd[0], temp_buf, sizeof(temp_buf))) > 0) {
        if (total + n >= capacity) {
            // Grow buffer
            size_t new_capacity = capacity * 2;
            // Cap at some reasonable limit if needed, e.g. 1MB?
            // The user said "shouldnt really bother us to have super large size"

            char *new_output = allocate(new_capacity);
            if (!new_output) {
                log_error("allocate failed during growth");
                break; // Stop reading, return what we have
            }

            memcpy(new_output, output, total);
            deallocate(output);
            output = new_output;
            capacity = new_capacity;
        }

        memcpy(output + total, temp_buf, n);
        total += n;
    }

    // Ensure null termination (might need 1 more byte if full)
    if (total == capacity) {
        // This is a rare edge case where we filled exactly.
        // We need 1 byte for 0. Allocating just 1 byte more essentially means
        // growing substantially in arena terms (min chunk), but we must do it.
        // Or we can just grow by small amount.
        size_t new_capacity = capacity + 512; // Grow by one chunk
        char *new_output = allocate(new_capacity);
        if (new_output) {
            memcpy(new_output, output, total);
            deallocate(output);
            output = new_output;
            // capacity = new_capacity;
        } else {
            // If fails, we truncate the last byte to fit null?
            // Or strict fail? Truncating is safer for stability.
            total--;
        }
    }
    output[total] = '\0';
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    gettimeofday(&end, NULL);
    double duration = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;

    int exit_code = 0;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else {
        exit_code = -1;
    }

    log_exec(path, exit_code, duration);
    *exit_code_out = exit_code;

    if (exit_code == 127) {
        deallocate(output);
        return NULL;
    }

    return output;
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
