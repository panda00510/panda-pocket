#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nav.h"
#include "pp_config.h"
#include "pp_http.h"

#define RESP_MAX 24576
#define TOKEN_MAX 1024   /* a OneMap JWT is ~570 characters */

static char s_token[TOKEN_MAX];   /* renewed token; empty: use the one compiled in */

static const char *token(void) { return s_token[0] ? s_token : CONFIG_PP_ONEMAP_TOKEN; }

/* Gets a fresh token with the configured account. false when no account is set or the call fails. */
static bool renew_token(char *buf)
{
    if (!CONFIG_PP_ONEMAP_EMAIL[0]) return false;
    char body[256];
    onemap_token_request(body, sizeof body, CONFIG_PP_ONEMAP_EMAIL, CONFIG_PP_ONEMAP_PASSWORD);
    int status;
    if (pp_http_post_json("https://www.onemap.gov.sg/api/auth/post/getToken", body, buf, RESP_MAX, &status) != PP_HTTP_OK) return false;
    return onemap_token_parse(buf, s_token, sizeof s_token) == 0;
}

nav_err_t nav_route(int from, int to, nav_route_t *out, bool *from_mock)
{
    bool mock = PP_NAV_USES_MOCK;
    *from_mock = mock;
    if (mock) return nav_mock_route(from, to, out) ? NAV_ERR_PARSE : NAV_OK;

    char *buf = malloc(RESP_MAX);
    if (!buf) return NAV_ERR_NET;
    char url[192];
    snprintf(url, sizeof url,
             "https://www.onemap.gov.sg/api/public/routingsvc/route?start=%.5f,%.5f&end=%.5f,%.5f&routeType=walk",
             NAV_PLACES[from].lat, NAV_PLACES[from].lng, NAV_PLACES[to].lat, NAV_PLACES[to].lng);

    nav_err_t r = NAV_ERR_AUTH;
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!token()[0] && !renew_token(buf)) break;   /* nothing to authenticate with */
        int status;
        pp_http_err_t e = pp_http_get(url, "Authorization", token(), buf, RESP_MAX, &status);
        if (e == PP_HTTP_OK) {
            r = nav_parse(buf, out) ? NAV_ERR_PARSE : NAV_OK;
            break;
        }
        if (e != PP_HTTP_ERR_STATUS) { r = NAV_ERR_NET; break; }
        /* OneMap: a bad or expired token is 401 {"message":"Unauthorized"}; renew it once and try again. */
        r = status == 401 || status == 403 ? NAV_ERR_AUTH : NAV_ERR_HTTP;
        if (r != NAV_ERR_AUTH || attempt || !renew_token(buf)) break;
    }
    free(buf);
    return r;
}
