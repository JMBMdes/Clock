#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT  BIT0

extern EventGroupHandle_t g_wifi_event_group;

void wifi_manager_init(void);
void wifi_manager_start_sta(const char *ssid, const char *pass);
void wifi_manager_start_ap(const char *ap_ssid);
bool wifi_manager_wait_connected(uint32_t timeout_ms);
