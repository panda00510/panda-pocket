#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bus.h"
#include "esp_log.h"
#include "pp_http.h"
#include "pp_config.h"

#define RESP_MAX 16384   /* 20-odd services is ~10 KB */

static bus_arrivals_t s_cache;
static time_t s_cache_at;
static char *s_buf;   /* static-ish: avoid 16 KB on a task stack */

bus_err_t bus_fetch(const char *stop, bus_arrivals_t *out, bool *from_mock)
{
    if (!bus_stop_valid(stop)) return BUS_ERR_STOP;
    time_t now = time(NULL);
    if (s_cache_at && !strcmp(s_cache.stop, stop) && now - s_cache_at < BUS_MIN_REFRESH_S) {
        *out = s_cache;
        return BUS_OK;
    }
    if (!s_buf && !(s_buf = malloc(RESP_MAX))) return BUS_ERR_NET;

    bool mock = PP_BUS_USES_MOCK;
    *from_mock = mock;
    if (mock) {
        bus_mock_json(s_buf, RESP_MAX, stop, now);
    } else {
        char url[96];
        snprintf(url, sizeof url, "https://datamall2.mytransport.sg/ltaodataservice/v3/BusArrival?BusStopCode=%s", stop);
        int status;
        pp_http_err_t e = pp_http_get(url, "AccountKey", CONFIG_PP_LTA_ACCOUNT_KEY, s_buf, RESP_MAX, &status);
        if (e == PP_HTTP_ERR_STATUS) return BUS_ERR_HTTP;
        if (e != PP_HTTP_OK) return BUS_ERR_NET;
    }
    if (bus_parse(s_buf, now, out) != 0) return BUS_ERR_PARSE;
    snprintf(out->stop, sizeof out->stop, "%s", stop);
    s_cache = *out;
    s_cache_at = now;
    return BUS_OK;
}
