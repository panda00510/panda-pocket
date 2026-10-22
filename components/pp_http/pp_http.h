#pragma once
#include <stddef.h>

typedef enum { PP_HTTP_OK, PP_HTTP_ERR_NET, PP_HTTP_ERR_STATUS, PP_HTTP_ERR_TOOBIG } pp_http_err_t;

/* HTTPS GET into buf (NUL-terminated). Optional one request header. 10 s timeout.
 * *status is the HTTP code when a response arrived (else 0). */
pp_http_err_t pp_http_get(const char *url, const char *hdr_name, const char *hdr_val, char *buf, size_t n,
                          int *status);
