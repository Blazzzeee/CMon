#include "utils.h"
#include "arena.h" // Assuming existing arena.h/c are still used, but we declared wrappers in utils.h
#include <event2/buffer.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

// Re-exporting arena constants/funcs if needed, or just using them.
// For now, let's assume main.c handles arena lifecycle, or we call arena functions directly.
// But utils.h declared wrappers, so let's implement them or include arena.h in utils.c

// Logging helpers
static void log_common(const char *level, const char *msg, const char *extra) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", t);

    // Stderr logging
    if (extra) {
        fprintf(stderr, "%s %s: %s %s\n", buf, level, msg, extra);
    } else {
        fprintf(stderr, "%s %s: %s\n", buf, level, msg);
    }

    // Syslog logging
    int priority = LOG_INFO;
    if (strcmp(level, "WARN") == 0)
        priority = LOG_WARNING;
    else if (strcmp(level, "ERROR") == 0)
        priority = LOG_ERR;

    if (extra) {
        syslog(priority, "%s: %s %s", level, msg, extra);
    } else {
        syslog(priority, "%s: %s", level, msg);
    }
}

void log_request(struct evhttp_request *req, const char *route) {
    const char *method = http_method_str(evhttp_request_get_command(req));
    const char *uri = evhttp_request_get_uri(req);

    char extra[512];
    snprintf(extra, sizeof(extra), "method=%s uri=\"%s\" route=\"%s\"", method, uri, route);
    log_common("INFO", "Request received", extra);
}

void log_exec(const char *cmd, int exit_code, double duration_sec) {
    char extra[256];
    snprintf(extra, sizeof(extra), "cmd=\"%s\" exit_code=%d duration=%.4fs", cmd, exit_code,
             duration_sec);
    const char *level = (exit_code == 0) ? "INFO" : "WARN";
    log_common(level, "Command executed", extra);
}

void log_error(const char *msg) { log_common("ERROR", msg, NULL); }

void log_info(const char *msg) { log_common("INFO", msg, NULL); }

const char *http_method_str(enum evhttp_cmd_type cmd_type) {
    switch (cmd_type) {
    case EVHTTP_REQ_GET:
        return "GET";
    case EVHTTP_REQ_POST:
        return "POST";
    case EVHTTP_REQ_PUT:
        return "PUT";
    case EVHTTP_REQ_PATCH:
        return "PATCH";
    case EVHTTP_REQ_HEAD:
        return "HEAD";
    case EVHTTP_REQ_DELETE:
        return "DELETE";
    case EVHTTP_REQ_OPTIONS:
        return "OPTIONS";
    case EVHTTP_REQ_TRACE:
        return "TRACE";
    case EVHTTP_REQ_CONNECT:
        return "CONNECT";
    default:
        return "Unknown method";
    }
}

char *json_escape(const char *input) {
    if (!input)
        return NULL;

    size_t needed = 0;
    const char *p = input;
    while (*p) {
        unsigned char c = *p;
        if (c == '\"' || c == '\\' || c == '\b' || c == '\f' || c == '\n' || c == '\r' ||
            c == '\t') {
            needed += 2;
        } else if (c < 32) {
            needed += 6; // \uXXXX
        } else {
            needed += 1;
        }
        p++;
    }
    needed += 1;

    char *output = allocate(needed);
    if (!output)
        return NULL;

    p = input;
    char *q = output;
    while (*p) {
        unsigned char c = *p;
        switch (c) {
        case '\"':
            *q++ = '\\';
            *q++ = '\"';
            break;
        case '\\':
            *q++ = '\\';
            *q++ = '\\';
            break;
        case '\b':
            *q++ = '\\';
            *q++ = 'b';
            break;
        case '\f':
            *q++ = '\\';
            *q++ = 'f';
            break;
        case '\n':
            *q++ = '\\';
            *q++ = 'n';
            break;
        case '\r':
            *q++ = '\\';
            *q++ = 'r';
            break;
        case '\t':
            *q++ = '\\';
            *q++ = 't';
            break;
        default:
            if (c < 32) {
                q += sprintf(q, "\\u%04x", c);
            } else {
                *q++ = c;
            }
        }
        p++;
    }
    *q = '\0';
    return output;
}

static char *create_response_template(int code, const char *status, const char *message,
                                      const char *data) {
    char *json = allocate(arena_buf_size);
    if (!json)
        return NULL;

    if (data) {
        char *escaped_data = json_escape(data);
        if (escaped_data) {
            snprintf(json, arena_buf_size,
                     "{\"status\": \"%s\", \"code\": %d, \"message\": \"%s\", \"data\": \"%s\"}",
                     status, code, message, escaped_data);
            deallocate(escaped_data);
        } else {
            deallocate(json);
            return NULL;
        }
    } else {
        snprintf(json, arena_buf_size,
                 "{\"status\": \"%s\", \"code\": %d, \"message\": \"%s\", \"data\": null}", status,
                 code, message);
    }
    return json;
}

void send_json_response(struct evhttp_request *req, int code, const char *status, const char *msg,
                        const char *data) {
    struct evbuffer *reply = evbuffer_new();
    char *json_resp = create_response_template(code, status, msg, data);

    if (json_resp) {
        evbuffer_add_printf(reply, "%s\n", json_resp);
        deallocate(json_resp);
    } else {
        evbuffer_add_printf(reply, "{\"error\": \"alloc failed\"}\n");
        code = 500;
    }

    evhttp_send_reply(req, (code == 200) ? HTTP_OK : HTTP_INTERNAL, NULL, reply);
    evbuffer_free(reply);
}

void send_json_error(struct evhttp_request *req, int code, const char *msg) {
    struct evbuffer *reply = evbuffer_new();
    char *json_resp = create_response_template(code, "error", msg, NULL);
    if (json_resp) {
        evbuffer_add_printf(reply, "%s\n", json_resp);
        deallocate(json_resp);
    }
    evhttp_send_reply(req, code, msg, reply);
    evbuffer_free(reply);
}

char *get_query_param(struct evhttp_request *req, const char *key) {
    if (!req || !key)
        return NULL;
    const char *uri = evhttp_request_get_uri(req);
    if (!uri)
        return NULL;

    struct evhttp_uri *decoded = evhttp_uri_parse(uri);
    if (!decoded)
        return NULL;

    const char *query = evhttp_uri_get_query(decoded);
    if (!query) {
        evhttp_uri_free(decoded);
        return NULL;
    }

    struct evkeyvalq params;
    evhttp_parse_query_str(query, &params);
    const char *val = evhttp_find_header(&params, key);

    char *result = NULL;
    if (val)
        result = strdup(val); // Caller must free

    evhttp_clear_headers(&params);
    evhttp_uri_free(decoded);
    return result;
}
