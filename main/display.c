#include "display.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lvgl_port.h"

static const char *TAG = "display";

/* -----------------------------------------------------------------------
 * Pin assignments — FSD Section 2.2 (assumed; verify against schematic)
 * ----------------------------------------------------------------------- */
#define PIN_MOSI  35
#define PIN_SCLK  36
#define PIN_CS    34
#define PIN_DC     4
#define PIN_RST    5
#define PIN_BLK    6

#define LCD_HOST            SPI2_HOST
#define LCD_BIT_PER_PIXEL   16
#define LCD_PIXEL_CLOCK_HZ  (40 * 1000 * 1000)  /* 40 MHz — safe starting point */
#define LCD_DRAW_BUF_LINES  50                   /* lines per LVGL draw buffer */

void display_init(void)
{
    /* --- SPI bus --- */
    ESP_LOGI(TAG, "Init SPI bus (MOSI=%d SCLK=%d)", PIN_MOSI, PIN_SCLK);
    const spi_bus_config_t buscfg = {
        .mosi_io_num     = PIN_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = PIN_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* --- LCD IO handle (SPI) --- */
    ESP_LOGI(TAG, "Init LCD SPI IO (CS=%d DC=%d)", PIN_CS, PIN_DC);
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = PIN_DC,
        .cs_gpio_num       = PIN_CS,
        .pclk_hz           = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io_handle));

    /* --- GC9A01 panel --- */
    ESP_LOGI(TAG, "Init GC9A01 panel (RST=%d)", PIN_RST);
    esp_lcd_panel_handle_t panel_handle = NULL;
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num  = PIN_RST,
        .rgb_ele_order   = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel  = LCD_BIT_PER_PIXEL,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle, &panel_cfg, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    /* --- Backlight on (GPIO, full brightness for Phase 1) --- */
    const gpio_config_t bl_cfg = {
        .pin_bit_mask = BIT64(PIN_BLK),
        .mode         = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&bl_cfg));
    gpio_set_level(PIN_BLK, 1);
    ESP_LOGI(TAG, "Backlight on (BLK=%d)", PIN_BLK);

    /* --- LVGL port --- */
    ESP_LOGI(TAG, "Init LVGL port");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle    = io_handle,
        .panel_handle = panel_handle,
        .buffer_size  = LCD_H_RES * LCD_DRAW_BUF_LINES,
        .double_buffer = false,
        .hres         = LCD_H_RES,
        .vres         = LCD_V_RES,
        .monochrome   = false,
        .rotation     = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,   /* DMA-capable buffer for SPI flush */
        },
    };
    lv_disp_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        abort();
    }
    ESP_LOGI(TAG, "Display ready (%dx%d)", LCD_H_RES, LCD_V_RES);
}
