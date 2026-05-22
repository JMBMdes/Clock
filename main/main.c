#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "display.h"
#include "clock_face.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "=== Clock — Phase 1 ===");
    ESP_LOGI(TAG, "Initializing display");
    display_init();

    ESP_LOGI(TAG, "Building clock face");
    clock_face_init();

    ESP_LOGI(TAG, "Running — TC-DISP-100 / TC-DISP-101 ready for inspection");

    /* Phase 1: static face; LVGL port task drives rendering.
     * Phase 2 will run clock_face_set_time() here every second. */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
