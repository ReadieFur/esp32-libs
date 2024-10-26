#pragma once

#include <freertos/FreeRTOS.h>
#include "AWaitHandle.hpp"
#include <freertos/task.h>
#include <freertos/FreeRTOSConfig.h>
#include <freertos/portmacro.h>

namespace ReadieFur::Event
{
    class CancellationTokenSource : private AWaitHandle
    {
    friend class SCancellationToken;
    public:
        struct SCancellationToken
        {
        friend class CancellationTokenSource;
        private:
            CancellationTokenSource* _cts;

            SCancellationToken(CancellationTokenSource* cts) : _cts(cts) {}

        public:
            SCancellationToken() : _cts(nullptr) {}

            bool IsCancellationRequested()
            {
                return _cts == nullptr || _cts->IsSet();
            }

            bool WaitForCancellation(TickType_t timeoutTicks = portMAX_DELAY)
            {
                if (_cts == nullptr)
                    return true;

                EventBits_t bitsSnapshot = xEventGroupWaitBits(
                    _cts->_eventGroup,
                    (1 << 0), //The bits to wait for.
                    pdFALSE, //Clear on exit (don't clear as this is a manual reset event).
                    pdTRUE, //Wait for all bits.
                    timeoutTicks
                );

                return (bitsSnapshot & (1 << 0)) == (1 << 0);
            }
        };
        
    private:
        struct STimeoutCallbackParams
        {
            CancellationTokenSource* self;
            TickType_t timeoutTicks;
        };

        static void TimeoutCallback(void* param)
        {
            STimeoutCallbackParams* params = reinterpret_cast<STimeoutCallbackParams*>(param);
            vTaskDelay(params->timeoutTicks);
            params->self->Set();
            vTaskDelete(NULL);
            delete params;
        }

        bool WaitOne(TickType_t) override { return true; }

    public:
        bool CancelAfter(TickType_t timeoutTicks)
        {
            char buf[configMAX_TASK_NAME_LEN];
            sprintf(buf, "cts%012d", xTaskGetTickCount());

            STimeoutCallbackParams* params = new STimeoutCallbackParams
            {
                .self = this,
                .timeoutTicks = timeoutTicks
            };
            if (xTaskCreate(TimeoutCallback, buf, configIDLE_TASK_STACK_SIZE + 64, this, configMAX_PRIORITIES * 0.1, nullptr) != pdPASS)
            {
                delete params;
                return false;
            }

            return true;
        }

        bool Cancel()
        {
            Set();

            return true;
        }

        SCancellationToken GetToken()
        {
            return SCancellationToken(this);
        }

        bool IsCancelled()
        {
            return IsSet();
        }
    };
}
