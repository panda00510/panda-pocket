#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nav.h"
#include "pp_http.h"
#include "sdkconfig.h"

#define RESP_MAX 24576

nav_err_t nav_route(int from, int to, nav_route_t *out, bool *from_mock)
{
    bool mock = CONFIG_PP_MOCK_MODE || CONFIG_PP_ONEMAP_TOKEN[0] == 0;
    *from_mock = mock;
    if (mock) return nav_mock_route(from, to, out) ? NAV_ERR_PARSE : NAV_OK;

    char *buf = malloc(RESP_MAX);
    if (!buf) return NAV_ERR_NET;
    char url[192];
    snprintf(url, sizeof url,
             "https://www.onemap.gov.sg/api/public/routingsvc/route?start=%.5f,%.5f&end=%.5f,%.5f&routeType=walk",
             NAV_PLACES[from].lat, NAV_PLACES[from].lng, NAV_PLACES[to].lat, NAV_PLACES[to].lng);
    int status;
    pp_http_err_t e = pp_http_get(url, "Authorization", CONFIG_PP_ONEMAP_TOKEN, buf, RESP_MAX, &status);
    nav_err_t r = e == PP_HTTP_OK ? (nav_parse(buf, out) ? NAV_ERR_PARSE : NAV_OK)
                : e == PP_HTTP_ERR_STATUS ? NAV_ERR_HTTP : NAV_ERR_NET;
    free(buf);
    return r;
}
