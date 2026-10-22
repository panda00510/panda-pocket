/* Mock demo: what the Bus and Navigation screens would show, on stdout. */
#include <stdio.h>
#include <time.h>

#include "bus.h"
#include "nav.h"

int main(void)
{
    static const char *load[] = { "?", "seats", "standing", "limited" };
    time_t now = time(NULL);
    char js[2048];
    bus_arrivals_t a;
    bus_mock_json(js, sizeof js, "83139", now);
    bus_parse(js, now, &a);
    printf("Bus stop %s [MOCK]\n", a.stop);
    for (int i = 0; i < a.n; i++) {
        printf("  %-4s", a.svc[i].service);
        for (int j = 0; j < 2; j++) {
            if (a.svc[i].eta_min[j] < 0) printf("  %-16s", "-");
            else printf("  %2d min %-9s", a.svc[i].eta_min[j], load[a.svc[i].load[j]]);
        }
        puts("");
    }
    nav_route_t r;
    nav_parse(nav_mock_json(), &r);
    printf("\nRoute preview (not live positioning) [MOCK]: %s -> %s, %d m, %d min\n", NAV_PLACES[0].name_en,
           NAV_PLACES[1].name_en, r.dist_m, r.time_s / 60);
    for (int i = 0; i < r.n_steps; i++) printf("  %d. %s (%d m)\n", i + 1, r.steps[i].text, r.steps[i].dist_m);
    return 0;
}
