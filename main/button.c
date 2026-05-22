#include "button.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <inttypes.h>

static const char *TAG = "button";

#define BUTTON_GPIO      0       /* GPIO0 / BOOT button — active low */
#define HOLD_THRESHOLD   5000   /* ms before callback fires */
#define POLL_PERIOD      50     /* ms between samples */

static button_cb_t s_cb;

static void button_task(void *arg)
{
    (void)arg;
    uint32_t held_ms = 0;

    while (1) {
        if (gpio_get_level(BUTTON_GPIO) == 0) {
            held_ms += POLL_PERIOD;
            if (held_ms >= HOLD_THRESHOLD) {
                ESP_LOGI(TAG, "CONFIG button held %" PRIu32 " ms — firing callback", held_ms);
                if (s_cb) s_cb();
                held_ms = 0;
                vTaskDelay(pdMS_TO_TICKS(2000));  /* prevent re-trigger */
            }
        } else {
            held_ms = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD));
    }
}

void button_init(button_cb_t cb)
{
    s_cb = cb;

    gpio_config_t io = {
        .pin_bit_mask = BIT64(BUTTON_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    xTaskCreate(button_task, "button_task", 2048, NULL, 3, NULL);
    ESP_LOGI(TAG, "Button task started (GPIO%d, hold %d ms)", BUTTON_GPIO, HOLD_THRESHOLD);
}
