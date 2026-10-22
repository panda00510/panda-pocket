#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define BUS_MAX_SERVICES 24
#define BUS_STOP_LEN 6   /* 5 digits + NUL */

typedef enum { BUS_LOAD_UNKNOWN, BUS_LOAD_SEATS, BUS_LOAD_STANDING, BUS_LOAD_LIMITED } bus_load_t;

typedef struct {
    char service[8];
    int16_t eta_min[2];   /* next, second; -1 = none, 0 = arriving */
    uint8_t load[2];      /* bus_load_t */
} bus_service_t;

typedef struct {
    char stop[BUS_STOP_LEN];
    int n;
    bus_service_t svc[BUS_MAX_SERVICES];
} bus_arrivals_t;

bool bus_stop_valid(const char *s);   /* exactly 5 digits */

/* LTA DataMall BusArrival v3 JSON -> out. `now` = current UTC epoch.
 * Returns 0, or -1 on bad JSON / missing Services. */
int bus_parse(const char *json, time_t now, bus_arrivals_t *out);

/* Builds a v3-shaped JSON for the mock, arrivals relative to `now`. */
void bus_mock_json(char *buf, size_t n, const char *stop, time_t now);

/* App-level entry: real API if a key is configured and mock is off, else mock.
 * Rate-limited: returns the cached result if called again within BUS_MIN_REFRESH_S. */
#define BUS_MIN_REFRESH_S 20
typedef enum { BUS_OK, BUS_ERR_STOP, BUS_ERR_NET, BUS_ERR_AUTH, BUS_ERR_HTTP, BUS_ERR_PARSE } bus_err_t;
bus_err_t bus_fetch(const char *stop, bus_arrivals_t *out, bool *from_mock);
