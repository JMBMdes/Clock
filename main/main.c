#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <stdio.h>

#include "display.h"
#include "clock_face.h"
#include "config.h"
#include "wifi_manager.h"
#include "sntp_sync.h"
#include "portal.h"
#include "dns_redirect.h"
#include "button.h"
#include "clock_task.h"

static const char *TAG = "main";

static void on_factory_reset(void)
{
    ESP_LOGW(TAG, "Factory reset triggered");
    clock_face_show_status("Resetting...");
    config_erase();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Clock — Phase 2 ===");

    /* NVS init */
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase, reformatting");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(nvs_err);
    }

    /* Load config (fills defaults when NVS is blank) */
    config_t cfg;
    config_load(&cfg);

    /* Display */
    display_init();
    clock_face_init();

    /* WiFi + SNTP or provisioning */
    wifi_manager_init();

    if (config_has_wifi(&cfg)) {
        /* --- STA mode --- */
        clock_face_show_status("Connecting...");
        wifi_manager_start_sta(cfg.wifi_ssid, cfg.wifi_pass);

        if (wifi_manager_wait_connected(15000)) {
            clock_face_show_status("Syncing time...");
            sntp_sync_start(cfg.ntp_server, cfg.timezone);
            /* status overlay clears once clock_task detects sync */
        } else {
            ESP_LOGW(TAG, "WiFi timeout — clock will run without NTP");
            clock_face_show_status("No WiFi");
            vTaskDelay(pdMS_TO_TICKS(2000));
            clock_face_show_status(NULL);
        }

        /* Hold GPIO0 for 5 s to factory-reset and re-enter provisioning */
        button_init(on_factory_reset);

        /* 1 Hz render loop */
        clock_task_start();

    } else {
        /* --- AP / provisioning mode (no credentials in NVS) --- */
        ESP_LOGI(TAG, "No WiFi credentials — entering provisioning mode");

        /* Build SSID from last 2 bytes of AP MAC (FR-4.3) */
        uint8_t mac[6] = {};
        esp_wifi_get_mac(WIFI_IF_AP, mac);  /* safe after esp_wifi_init */
        char ap_ssid[20];
        snprintf(ap_ssid, sizeof(ap_ssid), "CLOCK-%02X%02X", mac[4], mac[5]);

        clock_face_show_status("Setup mode");
        wifi_manager_start_ap(ap_ssid);
        dns_redirect_start();
        portal_start(&cfg);

        button_init(on_factory_reset);  /* also active in AP mode */

        /* Block here — portal handles config save + reboot */
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
