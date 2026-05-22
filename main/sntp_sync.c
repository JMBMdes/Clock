#include "sntp_sync.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include <time.h>
#include <stdlib.h>

static const char *TAG = "sntp_sync";

EventGroupHandle_t g_sntp_event_group;

static void sync_cb(struct timeval *tv)
{
    struct tm t;
    localtime_r(&tv->tv_sec, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    ESP_LOGI(TAG, "SNTP synced: %s (delta %+.3f s)", buf,
             (double)tv->tv_usec / 1e6);
    xEventGroupSetBits(g_sntp_event_group, SNTP_SYNCED_BIT);
}

void sntp_sync_start(const char *server, const char *tz_str)
{
    g_sntp_event_group = xEventGroupCreate();

    setenv("TZ", tz_str, 1);
    tzset();
    ESP_LOGI(TAG, "TZ set to: %s", tz_str);

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, server);
    sntp_set_time_sync_notification_cb(sync_cb);
    sntp_init();
    ESP_LOGI(TAG, "SNTP started, server: %s", server);
}

bool sntp_sync_is_synced(void)
{
    if (!g_sntp_event_group) return false;
    return (xEventGroupGetBits(g_sntp_event_group) & SNTP_SYNCED_BIT) != 0;
}
