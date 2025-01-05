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
            #else
            // auto a = ReadieFur::Service::ServiceManager::_services;
            return false;
            #endif
        }

    protected:
        void RunServiceImpl() override
        {
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
                    if (cpuLogString.ends_with(", "))
                        cpuLogString = cpuLogString.substr(0, cpuLogString.length() - 2);
                    cpuRecordings.clear();
                    LOGD(nameof(DiagnosticsService), "%s", cpuLogString.c_str());
                    cpuLogString.clear();
                }

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
