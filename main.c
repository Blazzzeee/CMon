#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include "arena.h"
#include "auth.h"
#include "commands.h"
#include "utils.h"

#define PORT 8000
#define AUTH_HEADER_KEY "access_token"

// -- Route Definition --
typedef struct {
    const char *path;
    enum evhttp_cmd_type method;
    void (*callback)(struct evhttp_request *req, void *ctx);
} route;

// -- Forward Declarations --
void health_callback(struct evhttp_request *req, void *ctx);
void reboot_callback(struct evhttp_request *req, void *ctx);
void restart_callback(struct evhttp_request *req, void *ctx);
void sync_upstream_callback(struct evhttp_request *req, void *ctx);
void deploy_branch_callback(struct evhttp_request *req, void *ctx);
void teardown_branch_callback(struct evhttp_request *req, void *ctx);
void logs_callback(struct evhttp_request *req, void *ctx);

// -- Route Table --
route ROUTES_CONFIG[] = {
    {"/health", EVHTTP_REQ_GET, health_callback},
    {"/reboot", EVHTTP_REQ_POST, reboot_callback},
    {"/restart", EVHTTP_REQ_POST, restart_callback},
    {"/sync_upstream", EVHTTP_REQ_PUT, sync_upstream_callback},
    {"/deploy_branch", EVHTTP_REQ_GET, deploy_branch_callback},
    {"/teardown_branch", EVHTTP_REQ_DELETE, teardown_branch_callback},
    {"/logs", EVHTTP_REQ_GET, logs_callback},
};

const size_t NUM_ROUTES = sizeof(ROUTES_CONFIG) / sizeof(route);

// -- Middleware --

void auth_middleware(struct evhttp_request *req, void *ctx) {
    size_t i = (size_t)(intptr_t)ctx;

    if (i >= NUM_ROUTES) {
        log_error("Dispatch error: invalid route index");
        send_json_error(req, 500, "Internal Server Error");
        return;
    }

    // Extract value from request header
    struct evkeyvalq *headers = evhttp_request_get_input_headers(req);
    const char *client_auth_key = evhttp_find_header(headers, AUTH_HEADER_KEY);

    if (!client_auth_key) {
        log_error("Middleware: Authentication Error (Missing Header)");
        send_json_error(req, 401, "Authentication Error");
        return;
    }

    if (!authenticate(client_auth_key)) {
        log_request(req, ROUTES_CONFIG[i].path);
        // DISPATCH TO ROUTE CALLBACK
        ROUTES_CONFIG[i].callback(req, ctx);
    } else {
        log_error("Middleware: Authentication Error (Invalid Key)");
        send_json_error(req, 401, "Authentication Error");
    }
}

// -- Helpers --

void validate_and_run(struct evhttp_request *req, void *ctx, char *(*runner)(int *)) {
    size_t i = (size_t)(intptr_t)ctx;
    if (evhttp_request_get_command(req) != ROUTES_CONFIG[i].method) {
        send_json_error(req, 405, "Method Not Allowed");
        return;
    }

    int exit_code = 0;
    char *output = runner(&exit_code);

    if (exit_code == 0) {
        send_json_response(req, 200, "ok", "Command executed", output);
    } else {
        send_json_response(req, 500, "error", "Command failed", output ? output : "Unknown error");
    }

    if (output)
        deallocate(output);
}

void validate_and_run_arg(struct evhttp_request *req, void *ctx,
                          char *(*runner)(const char *, int *), const char *arg_key) {
    size_t i = (size_t)(intptr_t)ctx;
    if (evhttp_request_get_command(req) != ROUTES_CONFIG[i].method) {
        send_json_error(req, 405, "Method Not Allowed");
        return;
    }

    char *arg = get_query_param(req, arg_key);

    int exit_code = 0;
    char *output = runner(arg, &exit_code);

    if (arg)
        free(arg);

    if (exit_code == 0) {
        send_json_response(req, 200, "ok", "Command executed", output);
    } else {
        send_json_response(req, 500, "error", "Command failed", output ? output : "Unknown error");
    }

    if (output)
        deallocate(output);
}

// -- Callbacks --

void health_callback(struct evhttp_request *req, void *ctx) {
    validate_and_run(req, ctx, run_health);
}

void reboot_callback(struct evhttp_request *req, void *ctx) {
    validate_and_run(req, ctx, run_reboot);
}

void restart_callback(struct evhttp_request *req, void *ctx) {
    validate_and_run(req, ctx, run_restart);
}

void logs_callback(struct evhttp_request *req, void *ctx) { validate_and_run(req, ctx, run_logs); }

void sync_upstream_callback(struct evhttp_request *req, void *ctx) {
    validate_and_run_arg(req, ctx, run_git_pull, "branch");
}

void deploy_branch_callback(struct evhttp_request *req, void *ctx) {
    validate_and_run_arg(req, ctx, run_deploy_branch, "branch");
}

void teardown_branch_callback(struct evhttp_request *req, void *ctx) {
    validate_and_run_arg(req, ctx, run_teardown_branch, "branch");
}

// -- Setup --

static void signal_cb(evutil_socket_t fd, short event, void *arg) {
    (void)event;
    printf("%s Shutting down server...\n", strsignal(fd));
    event_base_loopbreak(arg);
}

static void generic_request_handler(struct evhttp_request *req, void *ctx) {
    (void)ctx;
    log_info("Got request for unallowed path");
    send_json_error(req, 404, "Route not found");
}

int main() {
    prealloc_arena();

    if (init_auth()) {

        log_error("init auth failed \n");
    }
    openlog("cmon", LOG_PID | LOG_CONS, LOG_DAEMON);

    struct event_base *base = event_base_new();
    if (!base) {
        fprintf(stderr, "Error: Event base is null\n");
        return 1;
    }

    struct evhttp *http_server = evhttp_new(base);
    if (!http_server) {
        fprintf(stderr, "Error: Server is null\n");
        return 1;
    }

    if (evhttp_bind_socket(http_server, "0.0.0.0", PORT) != 0) {
        perror("Bind");
        return 1;
    }

    evhttp_set_allowed_methods(http_server, EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_PUT |
                                                EVHTTP_REQ_DELETE);

    for (size_t i = 0; i < NUM_ROUTES; ++i) {
        evhttp_set_cb(http_server, ROUTES_CONFIG[i].path, auth_middleware, (void *)(intptr_t)i);
    }

    evhttp_set_gencb(http_server, generic_request_handler, NULL);

    struct event *sig_int = evsignal_new(base, SIGINT, signal_cb, base);
    event_add(sig_int, NULL);

    printf("Listening requests on http://0.0.0.0:%d\n", PORT);
    event_base_dispatch(base);

    syslog(LOG_INFO, "Server stopping");
    closelog();
    teardown_arena();
    evhttp_free(http_server);
    event_free(sig_int);
    event_base_free(base);

    return 0;
}
