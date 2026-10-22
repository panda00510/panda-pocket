#include <stdio.h>
#include <string.h>

#include "bus.h"

static void iso(char *b, size_t n, time_t t)
{
    t += 8 * 3600;   /* render as SGT */
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(b, n, "%04d-%02d-%02dT%02d:%02d:%02d+08:00", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
}

void bus_mock_json(char *buf, size_t n, const char *stop, time_t now)
{
    static const struct { const char *no; int a, b; const char *la, *lb; } S[] = {
        { "15", 2, 11, "SEA", "SDA" },
        { "36", 0, 7, "SDA", "LSD" },
        { "133", 5, 14, "SEA", "SEA" },
        { "NR6", 9, -1, "LSD", "" },
    };
    size_t o = snprintf(buf, n, "{\"BusStopCode\":\"%s\",\"Services\":[", stop);
    for (size_t i = 0; i < sizeof S / sizeof *S && o < n; i++) {
        char t1[64] = "", t2[64] = "";
        if (S[i].a >= 0) iso(t1, sizeof t1, now + S[i].a * 60 + 30);
        if (S[i].b >= 0) iso(t2, sizeof t2, now + S[i].b * 60 + 30);
        o += snprintf(buf + o, n - o,
                      "%s{\"ServiceNo\":\"%s\",\"NextBus\":{\"EstimatedArrival\":\"%s\",\"Load\":\"%s\"},"
                      "\"NextBus2\":{\"EstimatedArrival\":\"%s\",\"Load\":\"%s\"}}",
                      i ? "," : "", S[i].no, t1, S[i].la, t2, S[i].lb);
    }
    if (o < n) snprintf(buf + o, n - o, "]}");
}
