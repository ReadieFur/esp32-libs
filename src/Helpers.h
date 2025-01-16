#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/FreeRTOSConfig.h>
#ifndef ARDUINO
#include <sdkconfig.h>
#endif
#include "Logging.hpp"
#include <stdint.h>

#define nameof(n) #n

#ifdef ARDUINO
#define IDLE_TASK_STACK_SIZE configIDLE_TASK_STACK_SIZE
#else
#define IDLE_TASK_STACK_SIZE CONFIG_FREERTOS_IDLE_TASK_STACKSIZE
#endif

#define ESP32_LIBS_VERSION_MAJOR UINT8_C(1)
#define ESP32_LIBS_VERSION_MINOR UINT8_C(0)
#define ESP32_LIBS_VERSION_PATCH UINT8_C(0)

#ifdef DEBUG
#define HALT() do {                 \
    vTaskSuspendAll();              \
    while (1) {                     \
        ets_delay_us(UINT32_MAX);   \
    }                               \
} while (0)
#endif
