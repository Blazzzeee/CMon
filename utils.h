#pragma once
#include <event2/buffer.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

// Arena declarations removed - use arena.h directly

// Logging
void log_request(struct evhttp_request *req, const char *route);
void log_exec(const char *cmd, int exit_code, double duration_sec);
void log_error(const char *msg);
void log_info(const char *msg);

// JSON / Response Helpers
void send_json_response(struct evhttp_request *req, int code, const char *status, const char *msg,
                        const char *data);
void send_json_error(struct evhttp_request *req, int code, const char *msg);
char *json_escape(const char *input);

// HTTP Helpers
const char *http_method_str(enum evhttp_cmd_type cmd_type);
char *get_query_param(struct evhttp_request *req, const char *key);
