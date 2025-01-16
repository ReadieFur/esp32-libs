#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include "AWaitHandle.hpp"
#include <vector>
#include <freertos/task.h>
#include "Helpers.h"

namespace ReadieFur::Event
{
    class Waitable
    {
    private:
        Waitable() {}

    public:
        static bool WaitAll(std::vector<AWaitHandle*> waitHandles, TickType_t timeout = portMAX_DELAY)
        {
            if (waitHandles.empty())
                return true;

            TickType_t start = xTaskGetTickCount();

            for (auto &&waitHandle : waitHandles)
            {
                // if (waitHandle == nullptr)
                //     continue;

                TickType_t remaining = timeout - (xTaskGetTickCount() - start);
                if (!waitHandle->WaitOne(remaining))
                    return false;

                portYIELD();
            }

            return true;
        }

        static bool WaitAny(std::vector<AWaitHandle*> waitHandles, TickType_t timeout = portMAX_DELAY)
        {
            if (waitHandles.empty())
                return true;

            TickType_t start = xTaskGetTickCount();

            //TODO: Make a task for each event group and wait for the first one to finish (current trouble is signaling to the other tasks to end as they will be blocked).
            //Current solution is a busy loop which is not ideal.
            while (true)
            {
                for (auto &&waitHandle : waitHandles)
                {
                    TickType_t remaining = timeout - (xTaskGetTickCount() - start);
                    if (remaining <= 0)
                        return false;

                    if (waitHandle->WaitOne(0))
                        return true;

                    portYIELD(); //Allow other tasks to run between this busy loop.
                }
            }
        }
    };
};
