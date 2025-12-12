#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "bsp/esp-bsp.h"
#include "lvgl.h"

#include "driver/ledc.h"

#define LCD_LEDC_TIMER      LEDC_TIMER_0
#define LCD_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LCD_LEDC_CHANNEL    LEDC_CHANNEL_0
#define LCD_LEDC_DUTY_RES   LEDC_TIMER_10_BIT   // 10bit (0〜1023)
#define LCD_LEDC_FREQUENCY  500                 // AtomS3は 500Hz 推奨 
#define LCD_BL_GPIO         16  

#define GRID_SIZE 4

static lv_display_t *disp = NULL;
static const char *TAG = "app_main";

static lv_obj_t *cells[GRID_SIZE][GRID_SIZE];

esp_err_t my_backlight_init(void);
esp_err_t my_backlight_set(int percent);

void app_main(void)
{
    disp = bsp_display_start();
    ESP_ERROR_CHECK(my_backlight_init());
    ESP_ERROR_CHECK(my_backlight_set(20));  
    ESP_LOGI(TAG, "Display initialized");

    while (true)
    {
        bsp_display_lock(0);

        lv_obj_t *scr = lv_disp_get_scr_act(disp);

        lv_coord_t w = lv_obj_get_width(scr);
        lv_coord_t h = lv_obj_get_height(scr);

        lv_coord_t cell_w = w / GRID_SIZE;
        lv_coord_t cell_h = h / GRID_SIZE;
        
        for (int row = 0; row < GRID_SIZE; row++) {
            for (int col = 0; col < GRID_SIZE; col++) {

                lv_obj_t *cell = lv_obj_create(scr);
                cells[row][col] = cell;

                // 余計な枠や影を消す（見た目をシンプルに）
                lv_obj_remove_style_all(cell);

                // サイズ設定（少しマージンを引く）
                lv_obj_set_size(cell, cell_w - 2, cell_h - 2);

                // 位置設定（行・列から座標計算）
                lv_obj_set_pos(cell,
                            col * cell_w + 1,
                            row * cell_h + 1);

                // 最初の色
                lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
                lv_obj_set_style_bg_color(cell,
                                        lv_palette_main(LV_PALETTE_BLUE),
                                        0);
            }
        }
        bsp_display_unlock();
    }
}

esp_err_t my_backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode       = LCD_LEDC_MODE,
        .duty_resolution  = LCD_LEDC_DUTY_RES,
        .timer_num        = LCD_LEDC_TIMER,
        .freq_hz          = LCD_LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t ch = {
        .speed_mode     = LCD_LEDC_MODE,
        .channel        = LCD_LEDC_CHANNEL,
        .timer_sel      = LCD_LEDC_TIMER,
        .gpio_num       = LCD_BL_GPIO,
        .intr_type      = LEDC_INTR_DISABLE,
        .duty           = 0,      // 初期は消灯
        .hpoint         = 0,
        .flags.output_invert = 0, // 必要なら 1 に変える
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));

    return ESP_OK;
}

esp_err_t my_backlight_set(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    uint32_t duty_max = (1 << LCD_LEDC_DUTY_RES) - 1;  // 1023
    uint32_t duty = duty_max * percent / 100;

    ESP_LOGI(TAG, "backlight = %d%% (duty=%" PRIu32 ")", percent, duty);

    ESP_ERROR_CHECK(ledc_set_duty(LCD_LEDC_MODE, LCD_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LCD_LEDC_MODE, LCD_LEDC_CHANNEL));
    return ESP_OK;
}