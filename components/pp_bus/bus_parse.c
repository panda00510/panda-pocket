#include "bus.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

bool bus_stop_valid(const char *s)
{
    if (!s || strlen(s) != 5) return false;
    for (int i = 0; i < 5; i++) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

/* "2026-10-22T10:30:00+08:00" -> UTC epoch; 0 on failure. */
static time_t iso_to_epoch(const char *s)
{
    int Y, M, D, h, m, sec, oh = 0, om = 0;
    char sign = 'Z';
    if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d%c%d:%d", &Y, &M, &D, &h, &m, &sec, &sign, &oh, &om) < 6) return 0;
    int off = (oh * 60 + om) * 60 * (sign == '-' ? -1 : 1);
    return (time_t)(days_from_civil(Y, M, D) * 86400 + h * 3600 + m * 60 + sec - off);
}

static void fill_bus(const cJSON *nb, time_t now, int16_t *eta, uint8_t *load)
{
    *eta = -1;
    *load = BUS_LOAD_UNKNOWN;
    const cJSON *ea = cJSON_GetObjectItem(nb, "EstimatedArrival");
    time_t t = cJSON_IsString(ea) ? iso_to_epoch(ea->valuestring) : 0;
    if (!t) return;
    long d = (long)(t - now);
    *eta = d <= 0 ? 0 : (int16_t)(d / 60);
    const cJSON *ld = cJSON_GetObjectItem(nb, "Load");
    if (cJSON_IsString(ld)) {
        if (!strcmp(ld->valuestring, "SEA")) *load = BUS_LOAD_SEATS;
        else if (!strcmp(ld->valuestring, "SDA")) *load = BUS_LOAD_STANDING;
        else if (!strcmp(ld->valuestring, "LSD")) *load = BUS_LOAD_LIMITED;
    }
}

int bus_parse(const char *json, time_t now, bus_arrivals_t *out)
{
    memset(out, 0, sizeof *out);
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    const cJSON *svcs = cJSON_GetObjectItem(root, "Services");
    if (!cJSON_IsArray(svcs)) { cJSON_Delete(root); return -1; }
    const cJSON *code = cJSON_GetObjectItem(root, "BusStopCode");
    if (cJSON_IsString(code)) snprintf(out->stop, sizeof out->stop, "%s", code->valuestring);
    const cJSON *s;
    cJSON_ArrayForEach(s, svcs) {
        if (out->n >= BUS_MAX_SERVICES) break;
        bus_service_t *o = &out->svc[out->n];
        const cJSON *no = cJSON_GetObjectItem(s, "ServiceNo");
        if (!cJSON_IsString(no)) continue;
        snprintf(o->service, sizeof o->service, "%s", no->valuestring);
        fill_bus(cJSON_GetObjectItem(s, "NextBus"), now, &o->eta_min[0], &o->load[0]);
        fill_bus(cJSON_GetObjectItem(s, "NextBus2"), now, &o->eta_min[1], &o->load[1]);
        out->n++;
    }
    cJSON_Delete(root);
    return 0;
}
