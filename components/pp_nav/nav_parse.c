#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "nav.h"

const nav_place_t NAV_PLACES[] = {
    { "raffles",  "Raffles Place MRT",   "莱佛士坊地铁站", 1.2840f, 103.8515f },
    { "esplanade","Esplanade",           "滨海艺术中心",   1.2897f, 103.8555f },
    { "gardens",  "Gardens by the Bay",  "滨海湾花园",     1.2816f, 103.8636f },
    { "orchard",  "Orchard MRT",         "乌节地铁站",     1.3040f, 103.8318f },
};
const int NAV_N_PLACES = sizeof NAV_PLACES / sizeof *NAV_PLACES;

/* Visits every point; keeps every k-th into pts (if non-NULL). Returns points seen. */
static int poly_walk(const char *s, nav_pt_t *pts, int max, int k, nav_pt_t *last)
{
    int la = 0, lo = 0, seen = 0, kept = 0;
    for (const char *p = s; *p; seen++) {
        int v[2];
        for (int j = 0; j < 2; j++) {
            int shift = 0, r = 0, b;
            do {
                if (!*p) return seen;   /* truncated */
                b = *p++ - 63;
                r |= (b & 0x1f) << shift;
                shift += 5;
            } while (b >= 0x20);
            v[j] = (r & 1) ? ~(r >> 1) : (r >> 1);
        }
        la += v[0];
        lo += v[1];
        *last = (nav_pt_t){ la / 1e5f, lo / 1e5f };
        if (pts && seen % k == 0 && kept < max) pts[kept++] = *last;
    }
    return seen;
}

int nav_polyline_decode(const char *s, nav_pt_t *pts, int max)
{
    nav_pt_t last = { 0, 0 };
    int total = poly_walk(s, NULL, 0, 1, &last);
    if (total == 0) return 0;
    int k = (total + max - 1) / max;
    int kept = (total + k - 1) / k;
    poly_walk(s, pts, max, k, &last);
    if ((total - 1) % k) pts[kept - 1 < max - 1 ? kept++ : max - 1] = last;   /* always end on the destination */
    return kept;
}

int nav_parse(const char *json, nav_route_t *out)
{
    memset(out, 0, sizeof *out);
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    const cJSON *sum = cJSON_GetObjectItem(root, "route_summary");
    const cJSON *ins = cJSON_GetObjectItem(root, "route_instructions");
    const cJSON *geo = cJSON_GetObjectItem(root, "route_geometry");
    if (!cJSON_IsObject(sum) || !cJSON_IsArray(ins)) { cJSON_Delete(root); return -1; }
    out->dist_m = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(sum, "total_distance"));
    out->time_s = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(sum, "total_time"));
    const cJSON *st;
    cJSON_ArrayForEach(st, ins) {
        if (out->n_steps >= NAV_MAX_STEPS) break;
        const cJSON *act = cJSON_GetArrayItem(st, 0), *road = cJSON_GetArrayItem(st, 1), *d = cJSON_GetArrayItem(st, 2);
        const cJSON *txt = cJSON_GetArrayItem(st, 9);   /* readable text, e.g. "Turn Right"; [0] is only the bare type */
        if (!cJSON_IsString(act)) continue;
        nav_step_t *o = &out->steps[out->n_steps++];
        const char *base = cJSON_IsString(txt) && *txt->valuestring ? txt->valuestring : act->valuestring;
        bool has_road = cJSON_IsString(road) && *road->valuestring;
        snprintf(o->text, sizeof o->text, "%s%s%s", base, has_road ? " " : "", has_road ? road->valuestring : "");
        o->dist_m = (int)cJSON_GetNumberValue(d);
    }
    if (cJSON_IsString(geo)) out->n_pts = nav_polyline_decode(geo->valuestring, out->pts, NAV_MAX_PTS);
    cJSON_Delete(root);
    return 0;
}
