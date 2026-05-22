#pragma once
#include "config.h"

/* Start the captive-portal HTTP server.
 * cfg is used as a template for form defaults and is updated on POST /config
 * before the device reboots.  Must be called after wifi_manager_start_ap(). */
void portal_start(config_t *cfg);
void portal_stop(void);
