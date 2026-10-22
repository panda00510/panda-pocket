#include "favs.h"

#include <string.h>

int favs_add(favs_t *f, const char *code)
{
    if (!bus_stop_valid(code)) return -1;
    for (int i = 0; i < f->n; i++) if (!strcmp(f->code[i], code)) return 0;
    if (f->n >= FAVS_MAX) return -1;
    strcpy(f->code[f->n++], code);
    return 0;
}

int favs_remove(favs_t *f, const char *code)
{
    for (int i = 0; i < f->n; i++) {
        if (strcmp(f->code[i], code)) continue;
        memmove(f->code[i], f->code[i + 1], (size_t)(f->n - i - 1) * BUS_STOP_LEN);
        f->n--;
        return 0;
    }
    return -1;
}
