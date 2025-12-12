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

#define MOTOR_PIN 1
#define PWM_FREQ_HZ     1000                // 1kHz
#define PWM_DUTY_RES    LEDC_TIMER_10_BIT   // 10bit (0〜1023)
#define PWM_TIMER       LEDC_TIMER_1
#define PWM_MODE        LEDC_LOW_SPEED_MODE
#define PWM_CHANNEL     LEDC_CHANNEL_1

static const char *TAG = "app_main";

esp_err_t my_backlight_init(void);
esp_err_t my_backlight_set(int percent);
void pwm_init(void);
void pwm_set_percent(int percent);

static lv_display_t *disp = NULL;

void app_main(void)
{

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

void pwm_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = PWM_MODE,
        .duty_resolution  = PWM_DUTY_RES,
        .timer_num        = PWM_TIMER,
        .freq_hz          = PWM_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    // 2) チャンネル設定（どのピンから出すか）
    ledc_channel_config_t channel = {
        .gpio_num       = MOTOR_PIN,
        .speed_mode     = PWM_MODE,
        .channel        = PWM_CHANNEL,
        .timer_sel      = PWM_TIMER,
        .duty           = 0,                 // 初期 duty = 0%
        .hpoint         = 0,
        .intr_type      = LEDC_INTR_DISABLE,
        .flags.output_invert = 0,           // 反転したければ 1
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

void pwm_set_percent(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    uint32_t duty_max = (1 << PWM_DUTY_RES) - 1;      // 2^10 - 1 = 1023
    uint32_t duty = duty_max * percent / 100;

    ESP_ERROR_CHECK(ledc_set_duty(PWM_MODE, PWM_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(PWM_MODE, PWM_CHANNEL));
}