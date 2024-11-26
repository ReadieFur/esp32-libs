#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

namespace ReadieFur::Event
{
    //https://freertos.org/Documentation/02-Kernel/04-API-references/12-Event-groups-or-flags/05-xEventGroupSetBits
    class AWaitHandle
    {
    protected:
        EventGroupHandle_t _eventGroup = xEventGroupCreate();

    public:
        ~AWaitHandle()
        {
            vEventGroupDelete(_eventGroup);
        }

        virtual bool WaitOne(TickType_t timeout = portMAX_DELAY) = 0;

        void Set()
        {
            //The return value if this method call can be used here to check if they were set, though for my use case I don't think I need to do this.
            xEventGroupSetBits(
                _eventGroup, //The event group being updated.
                (1 << 0) //The bits being set.
            ); 
        }

        BaseType_t SetFromISR(BaseType_t *higherPriorityTaskWoken)
        {
            return xEventGroupSetBitsFromISR(_eventGroup, (1 << 0), higherPriorityTaskWoken);
        }

        void Clear()
        {
            xEventGroupClearBits(_eventGroup, (1 << 0));
        }

        BaseType_t ClearFromISR()
        {
            return xEventGroupClearBitsFromISR(_eventGroup, (1 << 0));
        }

        bool IsSet()
        {
            return (xEventGroupGetBits(_eventGroup) & (1 << 0)) == (1 << 0);
        }

        bool IsSetISR()
        {
            return (xEventGroupGetBitsFromISR(_eventGroup) & (1 << 0)) == (1 << 0);
        }
    };
};
