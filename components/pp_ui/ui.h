#pragma once
#include "storage.h"

/* Builds Home and loads it. Call after hal_display_start(). `s` is owned by the caller (kept for the app's life). */
void ui_start(pp_settings_t *s);

#ifdef PP_SIM
/* Simulator only: open "menu", "bus", "nav" or "settings" directly (for screenshots). */
void ui_sim_open(const char *name);
void ui_sim_scroll_steps(int dy);
#endif
