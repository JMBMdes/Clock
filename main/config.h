#pragma once
#include <stdbool.h>
#include <stdint.h>

#define CFG_NVS_NAMESPACE  "clock_cfg"
#define CFG_SSID_MAX       64
#define CFG_PASS_MAX       64
#define CFG_NTP_MAX        64
#define CFG_TZ_MAX         48

typedef struct {
    char    wifi_ssid[CFG_SSID_MAX];
    char    wifi_pass[CFG_PASS_MAX];
    char    ntp_server[CFG_NTP_MAX];
    char    timezone[CFG_TZ_MAX];
    uint8_t brightness;
} config_t;

void config_load(config_t *cfg);
void config_save(const config_t *cfg);
void config_erase(void);
bool config_has_wifi(const config_t *cfg);
