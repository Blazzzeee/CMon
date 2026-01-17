#include "libevent-2.1.12-stable/include/event2/http.h"
#include <arpa/inet.h>
#include <cstddef>
#include <errno.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8000

#define DEBUG true

#define DO_PRAGMA(x) _Pragma(#x)

#ifdef DEBUG
#define TODO(msg) DO_PRAGMA(message("TODO: " msg))
#else
#define TODO(msg)
#endif

// Callbacks
static void todo_callback(struct evhttp_request *req, void *ctx);
void health_callback(struct evhttp_request *req, void *ctx);

// Internal types
struct {
    const char *path;
    enum evhttp_cmd_type method;
    void (*callback)(struct evhttp_request *req, void *);
    TODO("Add Command table")
} typedef route;

// Globals

// ROUTE/METHOD/CALLBACK CONFIG
route ROUTES_CONFIG[] = {
    {
        "/health",
        EVHTTP_REQ_GET,
        health_callback,
    },
    {
        "/reboot",
        EVHTTP_REQ_POST,
        todo_callback,
    },
    {
        "/restart",
        EVHTTP_REQ_POST,
        todo_callback,
    },
    {
        "/sync_upstream",
        EVHTTP_REQ_PUT,
        todo_callback,
    },
    {
        "/deploy_branch",
        EVHTTP_REQ_GET,
        todo_callback,
    },
    {
        "/teardown_branch",
        EVHTTP_REQ_DELETE,
        todo_callback,
    },
    {
        "/logs",
        EVHTTP_REQ_GET,
        todo_callback,
    },
};

size_t lengthRoutes = sizeof(ROUTES_CONFIG) / sizeof(route);

// Helpers

static inline const char *http_method_str(enum evhttp_cmd_type cmd_type) {
    switch (cmd_type) {
    case EVHTTP_REQ_GET:
        return "GET";
        break;
    case EVHTTP_REQ_POST:
        return "POST";
        break;

    case EVHTTP_REQ_PUT:
        return "PUT";
        break;

    case EVHTTP_REQ_PATCH:
        return "PATCH";
        break;

    case EVHTTP_REQ_HEAD:
        return "HEAD";
        break;

    case EVHTTP_REQ_DELETE:
        return "DELETE";
        break;

    case EVHTTP_REQ_OPTIONS:
        return "OPTIONS";
        break;

    case EVHTTP_REQ_TRACE:
        return "TRACE";
        break;

    case EVHTTP_REQ_CONNECT:
        return "CONNECT";
        break;

    default:
        return "Unknown method";
        break;
    }
}

int authenticate(struct evhttp_request *req) {
    TODO("Implement authentication")

error:
    evhttp_send_error(req, HTTP_BADREQUEST, NULL);

    return 0;
}

void health_callback(struct evhttp_request *req, void *ctx) {

    int i = (int)(intptr_t)ctx;
    TODO("Make this dry");
    char **route = &ROUTES_CONFIG[i].path;
    enum evhttp_cmd_type request_method = evhttp_request_get_command(req);
    enum evhttp_cmd_type allowed_route_method = ROUTES_CONFIG[i].method;
    int err = -1;
    int ret;
    fprintf(stderr, "Got request for route: %s \n", *route);

    // If request method is not allowed from event_config , send error to request
    if (request_method != allowed_route_method) {
        fprintf(stderr, "%s is not an allowed method on route %s \n",
                http_method_str(request_method), *route);
        goto error;
        err = 1;
    }

    // Enforce authentication
    if ((ret = authenticate((void *)req)) != 0) {
        fprintf(stderr, "Authentication error");
        err = 2;
        goto error;
    }

    TODO("Create and send health response")

error:
    if (1 == err) {
        evhttp_send_error(req, HTTP_BADMETHOD, NULL);
    } else if (2 == err) {
        evhttp_send_error(req, 401, "Authentication error\n");
    } else {
        evhttp_send_error(req, HTTP_INTERNAL, NULL);
    }
}

void todo_callback(struct evhttp_request *req, void *ctx) {
    TODO("Implement callback")

    // This callback is invoked with the parameter i
    // (i) maps which route was called
    // TODO: handle route based dispatch
    // TODO: implement generic dispatch table
    int i = (int)(intptr_t)ctx;
    char **path = &ROUTES_CONFIG[i].path;
    fprintf(stderr, "Got request for route: %s \n", *path);

    struct evbuffer *reply = evbuffer_new();
    evbuffer_add_printf(reply, "ACK: %s \n", *path);
    evhttp_send_reply(req, HTTP_OK, NULL, reply);
    evbuffer_free(reply);
}

static void signal_cb(evutil_socket_t fd, short event, void *arg) {

    (void)event;
    printf("%s Shutting down server...\n", strsignal(fd));
    event_base_loopbreak(arg);
}

static void generic_request_handler(struct evhttp_request *req, void *ctx) {
    // This is a generic callback invoked by the server on a http request , that does not match
    // evhttp_set_cb routes this method refuses every routes and methods that are not listed in
    // ROUTES
    (void)ctx;

    fprintf(stderr, "Got request for unallowed path \n");

    evhttp_send_error(req, HTTP_BADREQUEST, NULL);
}

int main() {
    ev_uint16_t http_port = PORT;
    char *http_addr = "0.0.0.0";
    struct event_base *base;
    struct evhttp *http_server;
    struct event *sig_int;
    int ret = 0;

    base = event_base_new();

    if (!base) {
        fprintf(stderr, "Error: Event base is null \n");
        return 1;
    }

    http_server = evhttp_new(base);

    if (!http_server) {
        fprintf(stderr, "Error: Server is null \n");
        return 1;
    }

    // Bind the server to system socket
    if ((ret = evhttp_bind_socket(http_server, http_addr, http_port)) != 0) {
        perror("Bind");
        return 1;
    }

    // Sets the what HTTP methods are supported in requests accepted by this
    // server, and passed to user callbacks.
    // unsupported requests return 501
    size_t ALLOWED_METHODS = EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_PUT | EVHTTP_REQ_DELETE;
    evhttp_set_allowed_methods(http_server, ALLOWED_METHODS);

    // Register all the routes with their callbacks to the server
    for (int i = 0; i < lengthRoutes; ++i) {
        if ((ret = evhttp_set_cb(http_server, ROUTES_CONFIG[i].path, ROUTES_CONFIG[i].callback,
                                 (void *)(intptr_t)i)) != 0) {
            perror("evhttp_set_cb: ");
        }
    }

    // Register generic http callback
    // This catches the request that will not be hanlded by any registered callbacks ,
    // registered with evhttp_set_cb
    // request returns 401 Bad request
    evhttp_set_gencb(http_server, generic_request_handler, NULL);

    // Handle SIGINT and in future and SIGKILL here
    sig_int = evsignal_new(base, SIGINT, signal_cb, base);
    if ((ret = event_add(sig_int, NULL)) != 0) {
        perror("event_add");
    }

    printf("Listening requests on http://%s:%d\n", http_addr, http_port);

    // Start the server event loop
    if ((ret = event_base_dispatch(base)) != 0) {
        perror("event_base_dispatch");
        return 1;
    }

    // Teardown
    evhttp_free(http_server);
    event_free(sig_int);
    event_base_free(base);
}
