#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include "AWaitHandle.hpp"

namespace ReadieFur::Event
{
    //https://freertos.org/Documentation/02-Kernel/04-API-references/12-Event-groups-or-flags/05-xEventGroupSetBits
    class ManualResetEvent : public AWaitHandle
    {
    public:
        bool WaitOne(TickType_t timeout = portMAX_DELAY) override
        {
            EventBits_t bitsSnapshot = xEventGroupWaitBits(
                _eventGroup,
                (1 << 0), //The bits to wait for.
                pdFALSE, //Clear on exit (don't clear as this is a manual reset event).
                pdTRUE, //Wait for all bits.
                timeout
            );

            //If the following evaluates to true then the wait was successful as the bits were set.
            return (bitsSnapshot & (1 << 0)) == (1 << 0);
        }
    };
};
