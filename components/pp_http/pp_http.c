#include "pp_http.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include <string.h>

#include "esp_log.h"

static const char *TAG = "http";

static pp_http_err_t request(esp_http_client_method_t method, const char *url, const char *hdr_name, const char *hdr_val,
                            const char *body, char *buf, size_t n, int *status)
{
    *status = 0;
    const esp_http_client_config_t cfg = {
        .url = url,
        .method = method,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return PP_HTTP_ERR_NET;
    if (hdr_name) esp_http_client_set_header(c, hdr_name, hdr_val);
    int blen = body ? (int)strlen(body) : 0;
    if (body) esp_http_client_set_header(c, "Content-Type", "application/json");
    pp_http_err_t r = PP_HTTP_ERR_NET;
    if (esp_http_client_open(c, blen) == ESP_OK && (!body || esp_http_client_write(c, body, blen) == blen)) {
        esp_http_client_fetch_headers(c);
        *status = esp_http_client_get_status_code(c);
        int len = esp_http_client_read_response(c, buf, (int)n - 1);
        if (len < 0) {
            ESP_LOGW(TAG, "read failed");
        } else if (*status != 200) {
            buf[len] = 0;   /* an error body can still be read by the caller */
            r = PP_HTTP_ERR_STATUS;
        } else if (!esp_http_client_is_complete_data_received(c)) {
            r = PP_HTTP_ERR_TOOBIG;   /* body larger than buf */
        } else {
            buf[len] = 0;
            r = PP_HTTP_OK;
        }
    }
    esp_http_client_cleanup(c);
    return r;
}

pp_http_err_t pp_http_get(const char *url, const char *hdr_name, const char *hdr_val, char *buf, size_t n, int *status)
{
    return request(HTTP_METHOD_GET, url, hdr_name, hdr_val, NULL, buf, n, status);
}

pp_http_err_t pp_http_post_json(const char *url, const char *body, char *buf, size_t n, int *status)
{
    return request(HTTP_METHOD_POST, url, NULL, NULL, body, buf, n, status);
}
