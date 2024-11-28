#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/FreeRTOSConfig.h>
#ifndef ARDUINO
#include <sdkconfig.h>
#endif
#include "Logging.hpp"

#define nameof(n) #n

#ifdef ARDUINO
#define IDLE_TASK_STACK_SIZE configIDLE_TASK_STACK_SIZE
#else
#define IDLE_TASK_STACK_SIZE CONFIG_FREERTOS_IDLE_TASK_STACKSIZE
#endif
