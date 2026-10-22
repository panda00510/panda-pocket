#pragma once
#include <stdbool.h>
#include <stddef.h>

#define NAV_MAX_STEPS 32
#define NAV_MAX_PTS 128

typedef struct { float lat, lng; } nav_pt_t;
typedef struct { char text[64]; int dist_m; } nav_step_t;

typedef struct {
    int dist_m, time_s;
    int n_steps;
    nav_step_t steps[NAV_MAX_STEPS];
    int n_pts;                 /* polyline, thinned to <= NAV_MAX_PTS */
    nav_pt_t pts[NAV_MAX_PTS];
} nav_route_t;

typedef struct { const char *id; const char *name_en, *name_zh; float lat, lng; } nav_place_t;

extern const nav_place_t NAV_PLACES[];
extern const int NAV_N_PLACES;

/* OneMap routingsvc walk response -> out. 0 ok, -1 bad JSON / no route.
 * Verified against a live response (host/tests/onemap_walk.json): each instruction is
 * [type, road, dist_m, "lat,lng", secs, "28m", dir, heading, mode, "Turn Right"]; road is often "". */
int nav_parse(const char *json, nav_route_t *out);

/* Google-style encoded polyline (1e5) -> pts, thinned to max. Returns count. */
int nav_polyline_decode(const char *s, nav_pt_t *pts, int max);

/* The built-in preview route (used by mock mode and tests). */
const char *nav_mock_json(void);

/* Estimated L-shaped walk between two presets (east/west leg, then north/south leg), for Mock Mode.
 * Not a real route: distance is the street-grid guess |dx|+|dy|. -1 if from == to or an index is out of range. */
int nav_mock_route(int from, int to, nav_route_t *out);

typedef enum { NAV_OK, NAV_ERR_NET, NAV_ERR_AUTH, NAV_ERR_HTTP, NAV_ERR_PARSE } nav_err_t;
/* Preset places by index. Mock when PP_MOCK_MODE or no token. */
nav_err_t nav_route(int from, int to, nav_route_t *out, bool *from_mock);
