#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "vl53l5cx_api.h"

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

uint8_t 				status, isAlive, isReady;
VL53L5CX_Configuration 	Dev;			/* Sensor configuration */
VL53L5CX_ResultsData 	Results;

esp_err_t my_backlight_init(void);
esp_err_t my_backlight_set(int percent);

esp_err_t vl53l5cx_settings_init(void);

void app_main(void)
{
    disp = bsp_display_start();
    ESP_ERROR_CHECK(my_backlight_init());
    ESP_ERROR_CHECK(my_backlight_set(50));  
    ESP_LOGI(TAG, "Display initialized");

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
            lv_obj_set_style_bg_opa(cell, 255, 0);
            lv_obj_set_style_bg_color(cell,
                                    lv_palette_main(LV_PALETTE_BLUE),
                                    0);
        }
    }
    bsp_display_unlock();
    ESP_ERROR_CHECK(vl53l5cx_settings_init());

    while (true)
    {
        status = vl53l5cx_check_data_ready(&Dev, &isReady);

        bsp_display_lock(0);
        if(isReady)
        {
            vl53l5cx_get_ranging_data(&Dev, &Results);

            for(int i = 0; i < 16; i++)
            {
                int alpha = 255 - (Results.distance_mm[VL53L5CX_NB_TARGET_PER_ZONE*i] * 255 / 1000);
                if (alpha > 255) alpha = 255;
                else if (alpha < 0) alpha = 0;
                lv_obj_set_style_bg_opa(cells[(int)(i / 4)][(int)(i % 4)], alpha, 0);
            }
        }
        bsp_display_unlock();

        VL53L5CX_WaitMs(&(Dev.platform), 5);
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

esp_err_t vl53l5cx_settings_init(void)
{
    i2c_port_t i2c_port = I2C_NUM_1;
    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = i2c_port,
        .scl_io_num = 2,
        .sda_io_num = 1,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &bus_handle));
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = VL53L5CX_DEFAULT_I2C_ADDRESS >> 1,
        .scl_speed_hz = VL53L5CX_MAX_CLK_SPEED,
    };

    Dev.platform.address = VL53L5CX_DEFAULT_I2C_ADDRESS;
    Dev.platform.bus_config = i2c_mst_config;

    i2c_master_bus_add_device(bus_handle, &dev_cfg, &Dev.platform.handle);

    /* (Optional) Check if there is a VL53L5CX sensor connected */
    status = vl53l5cx_is_alive(&Dev, &isAlive);
    if(!isAlive || status)
    {
        printf("VL53L5CX not detected at requested address\n");
        return ESP_FAIL;
    }

    /* (Mandatory) Init VL53L5CX sensor */
    status = vl53l5cx_init(&Dev);
    if(status)
    {
        printf("VL53L5CX ULD Loading failed\n");
        return ESP_FAIL;
    }
    status = vl53l5cx_set_resolution(&Dev, VL53L5CX_RESOLUTION_4X4);
    if(status) {
        printf("set_resolution failed: %u\n", status);
    }

    status = vl53l5cx_set_ranging_frequency_hz(&Dev, 30);
    if(status) {
        printf("set_ranging_frequency_hz failed: %u\n", status);
    }

    printf("VL53L5CX ULD ready ! (Version : %s)\n",
           VL53L5CX_API_REVISION);
           
    status = vl53l5cx_start_ranging(&Dev);

    return ESP_OK;
}