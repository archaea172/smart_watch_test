#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"
#include "vl53l5cx_api.h"

#define I2C_PORT           I2C_NUM_1
#define I2C_SDA_GPIO       1        // 自分の配線に合わせて変更
#define I2C_SCL_GPIO       2        // 自分の配線に合わせて変更

void app_main(void)
{

}