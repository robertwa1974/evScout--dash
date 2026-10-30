#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern SemaphoreHandle_t dataMutex;
extern SemaphoreHandle_t uiMutex;
// I2C_NUM_0 (GPIO8/9: touch, CH422G, IMU) - every runtime transaction on that
// bus, see imu_driver.h. Never take uiMutex while holding it.
extern SemaphoreHandle_t i2cMutex;

#ifdef __cplusplus
extern "C" {
#endif

void mutex_init();

#ifdef __cplusplus
} /*extern "C"*/
#endif