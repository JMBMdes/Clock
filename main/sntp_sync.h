#pragma once
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define SNTP_SYNCED_BIT  BIT0

extern EventGroupHandle_t g_sntp_event_group;

void sntp_sync_start(const char *server, const char *tz_str);
bool sntp_sync_is_synced(void);
