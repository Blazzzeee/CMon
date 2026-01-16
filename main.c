#include "libevent-2.1.12-stable/include/event2/http.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <arpa/inet.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>

#define PORT 8000

char *ROUTES[] = {
    "/health",
    "/reboot",
    "/restart",
    "/sync-upstream",
    "/deploy-branch", //default=main
    "/teardown-branch", //default=main
    "/logs" , //branch=? , gives snapshot of logs on branch deployed
};

uint8_t lengthRoutes = sizeof(ROUTES) / sizeof(char *);


static void generic_request_handler(struct evhttp_request *req, void *ctx)
{ 
    // This is a generic callback invoked by the server on a http request , that does not match evhttp_set_cb routes
    // TODO: this method should refuse every routes , and method
    struct evbuffer *reply = evbuffer_new();

    evbuffer_add_printf(reply, "It works!");
    evhttp_send_reply(req, HTTP_OK, NULL, reply);
    evbuffer_free(reply);
}

static void signal_cb(evutil_socket_t fd, short event, void *arg)
{
    printf("%s Shutting down server...\n", strsignal(fd));
    event_base_loopbreak(arg);
}


static void request_handler(struct evhttp_request *req , void *ctx)
{
    int i = (int ) (intptr_t) ctx;
    printf("Got request for route: %s", ROUTES[i]);

    struct evbuffer *reply = evbuffer_new();
    evbuffer_add_printf(reply, "ACK: %s", ROUTES[i]);
    evhttp_send_reply(req, HTTP_OK, NULL, reply);
    
}

int main()
{
    ev_uint16_t http_port = PORT;
    char *http_addr = "0.0.0.0";
    struct event_base *base;
    struct evhttp *http_server;
    struct event *sig_int;
    int ret = 0 ;


    base = event_base_new();

    http_server = evhttp_new(base);
    // Bind the server to system socket
    evhttp_bind_socket(http_server, http_addr, http_port);

    


    // Sets the what HTTP methods are supported in requests accepted by this
    // server, and passed to user callbacks.
    uint16_t ALLOWED_METHODS = EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_PATCH | EVHTTP_REQ_DELETE;
    evhttp_set_allowed_methods(http_server , ALLOWED_METHODS);

    
    // TODO: Register routes


    for (int i=0 ; i < lengthRoutes ; ++i){
        if ((ret=evhttp_set_cb(http_server, ROUTES[i],
                                request_handler, (void *)(intptr_t) i))!=0){
            perror("evhttp_set_cb");
        }
    }
    // This catches the request that will not be hanlded by any registered callbacks , registered with evhttp_set_cb
    // Register generic http callback
    evhttp_set_gencb(http_server, generic_request_handler, NULL);


    // Handle SIGINT and in future and SIGKILL here
    sig_int = evsignal_new(base, SIGINT, signal_cb, base);
    event_add(sig_int, NULL);


    printf("Listening requests on http://%s:%d\n", http_addr, http_port);


    // Start the server event loop
    event_base_dispatch(base);

    // Teardown
    evhttp_free(http_server);
    event_free(sig_int);
    event_base_free(base);
}
