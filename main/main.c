#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "vl53l5cx_api.h"

esp_err_t vl53l5cx_settings_init(void);


uint8_t 				status, isAlive, isReady;
VL53L5CX_Configuration 	Dev;			/* Sensor configuration */
VL53L5CX_ResultsData 	Results;

void app_main(void)
{
    ESP_ERROR_CHECK(vl53l5cx_settings_init());

    while(true)
    {
        /* Use polling function to know when a new measurement is ready.
         * Another way can be to wait for HW interrupt raised on PIN A1
         * (INT) when a new measurement is ready */

        status = vl53l5cx_check_data_ready(&Dev, &isReady);

        if(isReady)
        {
            vl53l5cx_get_ranging_data(&Dev, &Results);

            /* As the sensor is set in 4x4 mode by default, we have a total
             * of 16 zones to print. For this example, only the data of first zone are
             * print */
            printf("Print data no : %3u\n", Dev.streamcount);
            for(int i = 0; i < 16; i++)
            {
                printf("Zone : %3d, Status : %3u, Distance : %4d mm\n",
                       i,
                       Results.target_status[VL53L5CX_NB_TARGET_PER_ZONE*i],
                       Results.distance_mm[VL53L5CX_NB_TARGET_PER_ZONE*i]);
            }
            printf("\n");
        }

        /* Wait a few ms to avoid too high polling (function in platform
         * file, not in API) */
        WaitMs(&(Dev.platform), 5);
    }

    status = vl53l5cx_stop_ranging(&Dev);
    printf("End of ULD demo\n");
}

esp_err_t vl53l5cx_settings_init(void)
{
    i2c_port_t i2c_port = I2C_NUM_1;
    i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 1,
        .scl_io_num = 2,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = VL53L5CX_MAX_CLK_SPEED,
    };
    
    i2c_param_config(i2c_port, &i2c_config);
    i2c_driver_install(i2c_port, i2c_config.mode, 0, 0, 0);

    Dev.platform.address = VL53L5CX_DEFAULT_I2C_ADDRESS;
    Dev.platform.port = i2c_port;

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