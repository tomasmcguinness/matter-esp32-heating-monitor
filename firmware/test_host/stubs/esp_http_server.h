#pragma once
// Host stand-in for ESP-IDF's HTTP server: a request is a body to read, and the response is
// captured into a buffer for the test to inspect.
#include <stddef.h>
#include "esp_err.h"

typedef void *httpd_handle_t;
typedef enum { HTTP_GET, HTTP_POST } httpd_method_t;

typedef struct httpd_req {
    size_t      content_len;
    const char *body;
    size_t      read;
    char        status[64];
    char       *out;
    size_t      out_len;
} httpd_req_t;

typedef struct {
    const char    *uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t *req);
    void *user_ctx;
} httpd_uri_t;

#define HTTPD_RESP_USE_STRLEN -1

int       httpd_req_recv(httpd_req_t *req, char *buf, size_t len);
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type);
esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status);
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *field, const char *value);
esp_err_t httpd_resp_send_chunk(httpd_req_t *req, const char *buf, long len);
esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *req, const char *str);
esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *str);
esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, long len);
esp_err_t httpd_resp_send_500(httpd_req_t *req);
esp_err_t httpd_register_uri_handler(httpd_handle_t server, const httpd_uri_t *uri);
