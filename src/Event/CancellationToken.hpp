#pragma once

#include <freertos/FreeRTOS.h>
#include "Helpers.h"
#include "AWaitHandle.hpp"
#include <freertos/task.h>
#include <freertos/FreeRTOSConfig.h>
#include <freertos/portmacro.h>
#include <vector>

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
            bool _unreferenced;

            SCancellationToken(CancellationTokenSource* cts) : _cts(cts), _unreferenced(false) {}

        public:
            SCancellationToken() : _cts(nullptr), _unreferenced(true) {}

            bool IsCancellationRequested()
            {
                //TODO: _cts == nullptr has the potential to refer to a dangling pointer, fix this.
                return _unreferenced || _cts == nullptr || _cts->IsSet();
            }

            bool WaitForCancellation(TickType_t timeoutTicks = portMAX_DELAY)
            {
                if (_unreferenced || _cts == nullptr)
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

        std::vector<TaskHandle_t> _taskHandles;

        static void TimeoutCallback(void* param)
        {
            STimeoutCallbackParams* params = reinterpret_cast<STimeoutCallbackParams*>(param);
            vTaskDelay(params->timeoutTicks);
            if (ulTaskNotifyTake(pdFALSE, 0) == 0) params->self->Set();
            vTaskDelete(NULL);
            delete params;
        }

        bool WaitOne(TickType_t) override { return true; }

    public:
        virtual ~CancellationTokenSource()
        {
            for (auto &&handle : _taskHandles)
                if (eTaskGetState(handle) != eTaskState::eDeleted)
                    xTaskNotifyGive(handle);
            _taskHandles.~vector();
        }

        bool CancelAfter(TickType_t timeoutTicks)
        {
            char buf[configMAX_TASK_NAME_LEN];
            sprintf(buf, "cts%012d", xTaskGetTickCount());

            STimeoutCallbackParams* params = new STimeoutCallbackParams
            {
                .self = this,
                .timeoutTicks = timeoutTicks
            };
            TaskHandle_t handle;
            if (xTaskCreate(TimeoutCallback, buf, IDLE_TASK_STACK_SIZE + 64, this, configMAX_PRIORITIES * 0.1, &handle) != pdPASS)
            {
                delete params;
                return false;
            }

            _taskHandles.push_back(handle);

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
