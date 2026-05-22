#include "clock_task.h"
#include "clock_face.h"
#include "sntp_sync.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <time.h>
#include <string.h>

static const char *TAG = "clock_task";

static void clock_fn(void *arg)
{
    (void)arg;
    bool synced = false;
    TickType_t wake = xTaskGetTickCount();

    ESP_LOGI(TAG, "Clock task running");

    while (1) {
        time_t now;
        time(&now);
        struct tm t;
        localtime_r(&now, &t);

        bool is_synced = sntp_sync_is_synced();
        if (is_synced != synced) {
            synced = is_synced;
            clock_face_set_synced(synced);
            if (synced) {
                ESP_LOGI(TAG, "Display synced");
                clock_face_show_status(NULL);
            }
        }

        clock_face_set_time(t.tm_hour, t.tm_min, t.tm_sec);

        if (synced) {
            char date[20];
            strftime(date, sizeof(date), "%a %d %b", &t);
            /* uppercase */
            for (char *p = date; *p; p++)
                if (*p >= 'a' && *p <= 'z') *p -= 32;
            clock_face_set_date(date);
        }

        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000));
    }
}

void clock_task_start(void)
{
    xTaskCreatePinnedToCore(clock_fn, "clock_task", 8192, NULL, 5, NULL, 0);
}
