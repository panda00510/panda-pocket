#pragma once
#include "bus.h"

#define FAVS_MAX 8

typedef struct {
    int n;
    char code[FAVS_MAX][BUS_STOP_LEN];
} favs_t;

int favs_add(favs_t *f, const char *code);      /* 0 ok (also if already there), -1 invalid or full */
int favs_remove(favs_t *f, const char *code);   /* 0 removed, -1 not found */
