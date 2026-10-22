#include "nav.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define WALK_MPS 1.25f   /* ponytail: flat walking speed, no crossings/hills; OneMap gives the real time */

const char *nav_mock_json(void)
{
    return "{\"status\":0,\"route_summary\":{\"total_time\":780,\"total_distance\":1030},"
           "\"route_geometry\":\"_xyF{nzxRoFkHgJoFkHgEoF{@\","
           "\"route_instructions\":[[\"Head northeast\",\"Raffles Place\",190,\"1.2840,103.8515\"],"
           "[\"Turn right\",\"Cecil Street\",260,\"1.2870,103.8542\"],"
           "[\"Turn left\",\"Fullerton Road\",420,\"1.2885,103.8552\"],"
           "[\"Arrive\",\"Esplanade\",0,\"1.2897,103.8555\"]]}";
}

int nav_mock_route(int from, int to, nav_route_t *out)
{
    if (from == to || from < 0 || to < 0 || from >= NAV_N_PLACES || to >= NAV_N_PLACES) return -1;
    memset(out, 0, sizeof *out);
    const nav_place_t *a = &NAV_PLACES[from], *b = &NAV_PLACES[to];
    float dy = (b->lat - a->lat) * 111320.f;
    float dx = (b->lng - a->lng) * 111320.f * cosf(a->lat * 0.0174533f);
    int ex = (int)fabsf(dx), ey = (int)fabsf(dy);
    out->dist_m = ex + ey;
    out->time_s = (int)(out->dist_m / WALK_MPS);
    if (ex > 0) snprintf(out->steps[out->n_steps++].text, sizeof out->steps[0].text, "Head %s", dx > 0 ? "east" : "west");
    if (ex > 0) out->steps[out->n_steps - 1].dist_m = ex;
    if (ey > 0) snprintf(out->steps[out->n_steps++].text, sizeof out->steps[0].text, "%s %s", ex > 0 ? "Turn" : "Head", dy > 0 ? "north" : "south");
    if (ey > 0) out->steps[out->n_steps - 1].dist_m = ey;
    snprintf(out->steps[out->n_steps++].text, sizeof out->steps[0].text, "Arrive %s", b->name_en);
    out->pts[0] = (nav_pt_t){ a->lat, a->lng };
    out->pts[1] = (nav_pt_t){ a->lat, b->lng };   /* the corner */
    out->pts[2] = (nav_pt_t){ b->lat, b->lng };
    out->n_pts = 3;
    return 0;
}
