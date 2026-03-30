#pragma once

#include "Service/AService.hpp"
#include <stdlib.h>
#include <map>
#include <esp_heap_caps.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include "Logging.hpp"
#include "Helpers.h"
#include <freertos/FreeRTOSConfig.h>
#include <string>
#include <Service/ServiceManager.hpp>
#include <driver/temperature_sensor.h>

namespace ReadieFur::Diagnostic
{
    class DiagnosticsService : public ReadieFur::Service::AService
    {
    private:
        static bool GetCpuTime(std::map<BaseType_t, int32_t>& outRecordings)
        {
            #if configUSE_TRACE_FACILITY == 1
            //Get idle time for all CPU cores.
            UBaseType_t arraySize = uxTaskGetNumberOfTasks();
            TaskStatus_t* tasksArray = (TaskStatus_t*)malloc(arraySize * sizeof(TaskStatus_t));

            if (tasksArray == nullptr)
                return false;

            arraySize = uxTaskGetSystemState(tasksArray, arraySize, NULL);
            for (int i = 0; i < arraySize; i++)
                if (strcmp(tasksArray[i].pcTaskName, "IDLE") == 0)
                    outRecordings[tasksArray[i].xTaskNumber] = tasksArray[i].ulRunTimeCounter;

            free(tasksArray);
            return true;
            #elif INCLUDE_xTaskGetIdleTaskHandle == 1 && false
            for (size_t i = 0; i < configNUM_CORES; i++)
            {
                TaskHandle_t handle = xTaskGetIdleTaskHandleForCPU(i);
                if (handle == NULL)
                {
                    outRecordings[0] = -1;
                    continue;
                }
                return false;
            }
            #elif configGENERATE_RUN_TIME_STATS == 1 && configUSE_STATS_FORMATTING_FUNCTIONS == 1
            ulTaskGetIdleRunTimeCounter()
            return false;
            #else
            return false;
            #endif
        }

        static bool GetCpuTemperature(temperature_sensor_handle_t tempSensor, float& outTemperature)
        {
            return temperature_sensor_get_celsius(tempSensor, &outTemperature) == ESP_OK;
        }

        static void GetFreeMemory(size_t& outIram, size_t& outDram)
        {
            outIram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            outDram = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        }

        static bool GetTasksFreeStack(std::map<const char*, size_t>& outRecordings)
        {
            #if configUSE_TRACE_FACILITY == 1
            UBaseType_t arraySize = uxTaskGetNumberOfTasks();
            TaskStatus_t* tasksArray = (TaskStatus_t*)malloc(arraySize * sizeof(TaskStatus_t));

            if (tasksArray == nullptr)
                return false;

            //Get system state (task status).
            UBaseType_t totalTasks = uxTaskGetSystemState(tasksArray, arraySize, NULL);

            //Print free stack space for each task.
            for (UBaseType_t i = 0; i < totalTasks; i++)
                outRecordings[tasksArray[i].pcTaskName] = tasksArray[i].usStackHighWaterMark * sizeof(StackType_t);

            //Free allocated memory for taskStatusArray.
            free(tasksArray);
            return true;
            #else
            // auto a = ReadieFur::Service::ServiceManager::_services;
            return false;
            #endif
        }

    protected:
        void RunServiceImpl() override
        {
            esp_err_t err;
            temperature_sensor_handle_t tempSensor = NULL;
            temperature_sensor_config_t tempSensorConfig = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
            if ((err = temperature_sensor_install(&tempSensorConfig, &tempSensor)) != ESP_OK || (err = temperature_sensor_enable(tempSensor)) != ESP_OK)
            {
                LOGE(nameof(DiagnosticsService), "Failed to install temperature sensor driver: %s", esp_err_to_name(err));
                return;
            }

            while (!ServiceCancellationToken.IsCancellationRequested())
            {
                std::map<BaseType_t, int32_t> cpuRecordings;
                if (GetCpuTime(cpuRecordings))
                {
                    std::string cpuLogString;
                    for (auto &&recording : cpuRecordings)
                    {
                        cpuLogString += "CPU";
                        cpuLogString += recording.first;
                        cpuLogString += ": ";
                        cpuLogString += recording.second;
                        cpuLogString += ", ";
                    }
                    //Remove trailing comma and space if they exist.
                    // if (cpuLogString.ends_with(", "))
                    //     cpuLogString = cpuLogString.substr(0, cpuLogString.length() - 2);
                    cpuRecordings.clear();
                    LOGD(nameof(DiagnosticsService), "%s", cpuLogString.c_str());
                    cpuLogString.clear();
                }

                float cpuTemp;
                if (GetCpuTemperature(tempSensor, cpuTemp))
                    LOGD(nameof(DiagnosticsService), "CPU Temperature: %.02f°C", cpuTemp);

                size_t iram, dram;
                GetFreeMemory(iram, dram);
                LOGD(nameof(DiagnosticsService), "Memory free: IRAM: %u, DRAM: %u", iram, dram);

                std::map<const char*, size_t> taskRecordings;
                if (GetTasksFreeStack(taskRecordings))
                {
                    std::string tasksLogString;
                    for (auto &&recording : taskRecordings)
                    {
                    }
                }

                vTaskDelay(pdMS_TO_TICKS(5 * 1000));
            }
        }
    
    public:
        DiagnosticsService()
        {
            ServiceEntrypointStackDepth += 1024;
        }
    };
};
