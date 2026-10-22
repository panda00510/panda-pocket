/* Host self-check for the pure parsers: `make -C host test`. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bus.h"
#include "favs.h"
#include "ap_list.h"
#include "nav.h"

int main(void)
{
    /* stop code validation */
    assert(bus_stop_valid("83139") && !bus_stop_valid("8313") && !bus_stop_valid("8313a") && !bus_stop_valid(NULL));

    /* bus: mock JSON -> parse -> minutes/load */
    time_t now = 1790000000;
    char js[2048];
    bus_mock_json(js, sizeof js, "83139", now);
    bus_arrivals_t a;
    assert(bus_parse(js, now, &a) == 0 && a.n == 4 && !strcmp(a.stop, "83139"));
    assert(!strcmp(a.svc[0].service, "15") && a.svc[0].eta_min[0] == 2 && a.svc[0].eta_min[1] == 11);
    assert(a.svc[0].load[0] == BUS_LOAD_SEATS && a.svc[0].load[1] == BUS_LOAD_STANDING);
    assert(a.svc[1].eta_min[0] == 0 && a.svc[1].load[1] == BUS_LOAD_LIMITED);
    assert(a.svc[3].eta_min[1] == -1);
    assert(bus_parse("not json", now, &a) == -1 && bus_parse("{}", now, &a) == -1);
    /* offset handling: same instant written in UTC and SGT parses identically */
    const char *u = "{\"Services\":[{\"ServiceNo\":\"1\",\"NextBus\":{\"EstimatedArrival\":\"2026-10-22T02:10:00+00:00\"}}]}";
    const char *s = "{\"Services\":[{\"ServiceNo\":\"1\",\"NextBus\":{\"EstimatedArrival\":\"2026-10-22T10:10:00+08:00\"}}]}";
    bus_arrivals_t b;
    assert(bus_parse(u, 1792635000, &a) == 0 && bus_parse(s, 1792635000, &b) == 0 && a.svc[0].eta_min[0] == b.svc[0].eta_min[0]);

    /* nav: mock route */
    nav_route_t r;
    assert(nav_parse(nav_mock_json(), &r) == 0);
    assert(r.dist_m == 1030 && r.time_s == 780 && r.n_steps == 4 && r.n_pts == 5);
    assert(r.pts[0].lat > 1.283f && r.pts[0].lat < 1.285f && r.pts[4].lng > 103.855f && r.pts[4].lng < 103.856f);
    assert(!strcmp(r.steps[0].text, "Head North East Raffles Place") && !strcmp(r.steps[1].text, "Turn Right Cecil Street") && r.steps[3].dist_m == 0);
    assert(nav_parse("{}", &r) == -1);
    /* thinning keeps the destination */
    nav_pt_t few[3];
    assert(nav_polyline_decode("_xyF{nzxRoFkHgJoFkHgEoF{@", few, 3) == 3 && few[2].lng > 103.855f);

    /* a real OneMap walking response (captured 2026-10-22, Raffles Place -> Esplanade) */
    {
        FILE *f = fopen("tests/onemap_walk.json", "rb");
        assert(f);
        static char big[16384];
        size_t n = fread(big, 1, sizeof big - 1, f);
        big[n] = 0;
        fclose(f);
        static nav_route_t real;
        assert(nav_parse(big, &real) == 0 && real.dist_m == 1028 && real.time_s == 740 && real.n_steps == 14);
        assert(!strcmp(real.steps[0].text, "Head North") && !strcmp(real.steps[1].text, "Turn Right"));
        assert(real.n_pts > 20 && real.pts[0].lat > 1.283f && real.pts[0].lat < 1.285f);
    }

    /* mock route between presets: L-shaped estimate, symmetric, rejects from == to */
    nav_route_t m, m2;
    assert(nav_mock_route(0, 1, &m) == 0 && nav_mock_route(1, 0, &m2) == 0);
    assert(m.dist_m > 600 && m.dist_m < 1500 && m.time_s == (int)(m.dist_m / 1.25f));
    assert(abs(m.dist_m - m2.dist_m) <= 1 && m.n_pts == 3 && m.n_steps == 3);
    assert(!strcmp(m.steps[0].text, "Head east") && !strcmp(m.steps[1].text, "Turn north") && m.steps[2].dist_m == 0);
    assert(!strncmp(m2.steps[0].text, "Head west", 9) && !strcmp(m2.steps[1].text, "Turn south"));
    assert(nav_mock_route(2, 2, &m) == -1 && nav_mock_route(0, NAV_N_PLACES, &m) == -1 && nav_mock_route(-1, 0, &m) == -1);

    /* scan list: drop hidden, merge duplicates keeping the strongest, sort by signal */
    wifi_ap_t ap[] = { { "home", -70, true }, { "", -30, true }, { "cafe", -80, false }, { "home", -50, true }, { "new", -60, true } };
    int na = ap_list_clean(ap, 5);
    assert(na == 3 && !strcmp(ap[0].ssid, "home") && ap[0].rssi == -50 && !strcmp(ap[1].ssid, "new") && !strcmp(ap[2].ssid, "cafe") && !ap[2].secure);
    assert(ap_list_clean(ap, 0) == 0);

    /* favorites */
    favs_t f = { 0 };
    assert(favs_add(&f, "83139") == 0 && favs_add(&f, "83139") == 0 && f.n == 1);   /* dedupe */
    assert(favs_add(&f, "bad") == -1);
    for (int i = 0; i < FAVS_MAX + 2; i++) { char c[6]; snprintf(c, 6, "%05d", 10000 + i); favs_add(&f, c); }
    assert(f.n == FAVS_MAX);   /* full: extras refused, not overwritten */
    assert(favs_remove(&f, "83139") == 0 && f.n == FAVS_MAX - 1 && favs_remove(&f, "83139") == -1);

    puts("ok");
    return 0;
}
