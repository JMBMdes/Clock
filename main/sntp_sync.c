#include "sntp_sync.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include <time.h>

static const char *TAG = "sntp_sync";

EventGroupHandle_t g_sntp_event_group;

static void sync_cb(struct timeval *tv)
{
    struct tm t;
    localtime_r(&tv->tv_sec, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    ESP_LOGI(TAG, "SNTP synced: %s", buf);
    xEventGroupSetBits(g_sntp_event_group, SNTP_SYNCED_BIT);
}

void sntp_sync_start(const char *server, const char *tz_str)
{
    g_sntp_event_group = xEventGroupCreate();

    setenv("TZ", tz_str, 1);
    tzset();
    ESP_LOGI(TAG, "TZ set to: %s", tz_str);

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(server);
    config.sync_cb = sync_cb;
    config.num_of_servers = 2;
    config.servers[1] = "216.239.35.0";  /* Google NTP IP — fallback if DNS fails */
    esp_netif_sntp_init(&config);

    ESP_LOGI(TAG, "SNTP started, servers: %s + 216.239.35.0", server);
}

bool sntp_sync_is_synced(void)
{
    if (!g_sntp_event_group) return false;
    return (xEventGroupGetBits(g_sntp_event_group) & SNTP_SYNCED_BIT) != 0;
}
