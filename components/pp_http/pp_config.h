#pragma once
#include "sdkconfig.h"

/* Kconfig defines CONFIG_PP_MOCK_MODE only when it is "y"; with "n" the macro does not exist at all. */
#ifdef CONFIG_PP_MOCK_MODE
#define PP_MOCK 1
#else
#define PP_MOCK 0
#endif

/* True when bus (resp. route) data must come from the built-in fixtures. */
#define PP_BUS_USES_MOCK (PP_MOCK || CONFIG_PP_LTA_ACCOUNT_KEY[0] == 0)
/* A token or an account (to fetch one) is enough for live routes. */
#define PP_NAV_USES_MOCK (PP_MOCK || (CONFIG_PP_ONEMAP_TOKEN[0] == 0 && CONFIG_PP_ONEMAP_EMAIL[0] == 0))
