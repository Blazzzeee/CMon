#include <string.h>
#include <signal.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>

#define PORT 8000


static void generic_request_handler(struct evhttp_request *req, void *ctx)
{ 
    // This is a generic callback invoked by the server 
    // on a http request , 
    // TODO: this method should refuse unsupportted routes , unsupportted methods 
    // TODO: Redirect the callback to appropriate handler
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

int main()
{
    ev_uint16_t http_port = PORT;
    char *http_addr = "0.0.0.0";
    struct event_base *base;
    struct evhttp *http_server;
    struct event *sig_int;

    base = event_base_new();

    http_server = evhttp_new(base);
    // Bind the server to system socket
    evhttp_bind_socket(http_server, http_addr, http_port);
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
