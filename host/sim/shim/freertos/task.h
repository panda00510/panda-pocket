#pragma once
#include <pthread.h>
#include <stdlib.h>

typedef void (*TaskFunction_t)(void *);
typedef struct { TaskFunction_t fn; void *arg; } shim_task_t;

static void *shim_task_tramp(void *p)
{
    shim_task_t t = *(shim_task_t *)p;
    free(p);
    t.fn(t.arg);
    return NULL;
}

static inline int xTaskCreate(TaskFunction_t fn, const char *name, unsigned stack, void *arg, unsigned prio, void *h)
{
    shim_task_t *t = malloc(sizeof *t);
    pthread_t th;
    t->fn = fn;
    t->arg = arg;
    if (pthread_create(&th, NULL, shim_task_tramp, t)) { free(t); return 0; }
    pthread_detach(th);
    return 1;
}
static inline void vTaskDelete(void *h) { pthread_exit(NULL); }
