#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "config";

void config_load(config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->ntp_server, "pool.ntp.org", CFG_NTP_MAX - 1);
    strncpy(cfg->timezone,   "UTC0",         CFG_TZ_MAX  - 1);
    cfg->brightness = 80;

    nvs_handle_t h;
    if (nvs_open(CFG_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "No NVS config; using defaults");
        return;
    }

    size_t len;
    len = CFG_SSID_MAX; nvs_get_str(h, "wifi_ssid",  cfg->wifi_ssid,  &len);
    len = CFG_PASS_MAX; nvs_get_str(h, "wifi_pass",  cfg->wifi_pass,  &len);
    len = CFG_NTP_MAX;  nvs_get_str(h, "ntp_server", cfg->ntp_server, &len);
    len = CFG_TZ_MAX;   nvs_get_str(h, "timezone",   cfg->timezone,   &len);
    nvs_get_u8(h, "brightness", &cfg->brightness);
    nvs_close(h);

    ESP_LOGI(TAG, "Config loaded: ssid=%s ntp=%s tz=%s bright=%d",
             cfg->wifi_ssid, cfg->ntp_server, cfg->timezone, cfg->brightness);
}

void config_save(const config_t *cfg)
{
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(CFG_NVS_NAMESPACE, NVS_READWRITE, &h));
    ESP_ERROR_CHECK(nvs_set_str(h, "wifi_ssid",  cfg->wifi_ssid));
    ESP_ERROR_CHECK(nvs_set_str(h, "wifi_pass",  cfg->wifi_pass));
    ESP_ERROR_CHECK(nvs_set_str(h, "ntp_server", cfg->ntp_server));
    ESP_ERROR_CHECK(nvs_set_str(h, "timezone",   cfg->timezone));
    ESP_ERROR_CHECK(nvs_set_u8 (h, "brightness", cfg->brightness));
    ESP_ERROR_CHECK(nvs_commit(h));
    nvs_close(h);

    /* Never log the password — FR-5.7 */
    ESP_LOGI(TAG, "Config saved: ssid=%s ntp=%s tz=%s bright=%d",
             cfg->wifi_ssid, cfg->ntp_server, cfg->timezone, cfg->brightness);
}

void config_erase(void)
{
    nvs_handle_t h;
    if (nvs_open(CFG_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "NVS config erased");
}

bool config_has_wifi(const config_t *cfg)
{
    return cfg->wifi_ssid[0] != '\0';
}
