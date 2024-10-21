#pragma once

#include <freertos/FreeRTOS.h>
#include "EServiceResult.h"
#include <mutex>
#include <functional>
#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <algorithm>
#include <stack>
#include <unordered_set>
#include <freertos/FreeRTOSConfig.h>
#include <freertos/task.h>
#include <functional>
#include "Event/AutoResetEvent.hpp"
#include "Event/CancellationToken.hpp"

namespace ReadieFur::Service
{
    class AService
    {
    friend class ServiceManager;
    private:
        std::mutex _serviceMutex;
        std::function<AService*(std::type_index)> _getServiceCallback = nullptr;
        std::unordered_set<std::type_index> _dependencies = {};
        Event::AutoResetEvent _taskEndedEvent;
        TaskHandle_t* _taskHandle = nullptr;
        Event::CancellationTokenSource* _taskCts = nullptr;

        static void TaskWrapper(void* param)
        {
            AService* self = reinterpret_cast<AService*>(param);

            self->RunServiceImpl();

            self->_taskEndedEvent.Set();

            if (!self->_taskCts->IsCancelled() || eTaskGetState(NULL) != eTaskState::eDeleted)
            {
                //Consider the task as failed here, this occurs when the RunServiceImpl method returns before the task has been signalled for deletion.
                //For now, have the program fail like how tasks that end early in freertos call abort too.
                abort();
            }
            else
            {
                self->_taskEndedEvent.Set();
            }

            //Have the task clean itself up.
            // self->_serviceMutex.lock();

            // if (self->_taskHandle != nullptr)
            //     vTaskDelete(self->_taskHandle);
            // self->_taskHandle = nullptr;

            // delete self->_taskCts;
            // self->_taskCts = nullptr;

            // self->_taskEndedEvent.Set();

            // self->_serviceMutex.unlock();
        }

        void CleanupTask()
        {
            
        }

        // bool ContainsCircularDependency(AService* service)
        // {
        //     //This was originally a recursive method however I am using an iterative method to save space on the stack.
        //     std::unordered_set<AService*> visited;
        //     std::stack<AService*> toCheck;
        //     toCheck.push(service);

        //     while (!toCheck.empty())
        //     {
        //         AService* current = toCheck.top();
        //         toCheck.pop();

        //         //If we already visited this service then a circular dependency has been found.
        //         if (visited.count(current))
        //             return true;

        //         visited.insert(current);

        //         //Check dependencies of the current service.
        //         for (auto&& dependency : current->_installedDependencies)
        //         {
        //             if (dependency.second == this)
        //                 return true; //Circular dependency found.

        //             toCheck.push(dependency.second);
        //         }
        //     }

        //     return false;
        // }

        EServiceResult StartService()
        {
            _serviceMutex.lock();

            if (_taskHandle != nullptr)
            {
                _serviceMutex.unlock();
                return EServiceResult::Ok;
            }

            _taskCts = new Event::CancellationTokenSource();
            ServiceCancellationToken = _taskCts->GetToken();

            #if configNUM_CORES > 1
            BaseType_t taskCreateResult;
            if (ServiceEntrypointCore != -1 && ServiceEntrypointCore >= 0 && ServiceEntrypointCore < configNUM_CORES)
                taskCreateResult = xTaskCreatePinnedToCore(TaskWrapper, typeid(*this).name(), ServiceEntrypointStackDepth, this, ServiceEntrypointPriority, _taskHandle, ServiceEntrypointCore);
            else
            #endif
                taskCreateResult = xTaskCreate(TaskWrapper, typeid(*this).name(), ServiceEntrypointStackDepth, this, ServiceEntrypointPriority, _taskHandle);

            _serviceMutex.unlock();

            if (!taskCreateResult == pdPASS)
            {
                delete _taskCts;
                return EServiceResult::Failed;
            }
            return EServiceResult::Ok;
        }

        EServiceResult StopService(TickType_t timeout = portMAX_DELAY)
        {
            _serviceMutex.lock();
            
            if (_taskHandle == nullptr)
            {
                _serviceMutex.unlock();
                return EServiceResult::Ok;
            }

            vTaskDelete(_taskHandle); //Redundant.
            _taskCts->Cancel();
            if (!_taskEndedEvent.WaitOne(timeout))
            {
                _serviceMutex.unlock();
                return EServiceResult::Timeout;
            }

            if (!_taskEndedEvent.WaitOne(timeout))
            {
                _serviceMutex.unlock();
                return EServiceResult::Timeout;
            }

            delete _taskCts;
            _taskCts = nullptr;
            _taskHandle = nullptr;

            _serviceMutex.unlock();
            return EServiceResult::Ok;
        }

    protected:
        virtual EServiceResult RunServiceImpl() = 0;

        uint ServiceEntrypointPriority = configMAX_PRIORITIES * 0.1;
        uint ServiceEntrypointStackDepth = configIDLE_TASK_STACK_SIZE;
        int ServiceEntrypointCore = -1; //-1 to run on all cores.
        Event::CancellationTokenSource::SCancellationToken ServiceCancellationToken; //Defaults to true, which is ideal.

        template <typename T>
        typename std::enable_if<std::is_base_of<AService, T>::value, void>::type
        AddDependencyType()
        {
            _dependencies.insert(std::type_index(typeid(T)));
        }

        // template <typename T>
        // typename std::enable_if<std::is_base_of<AService, T>::value, void>::type
        // RemoveDependencyType()
        // {
        //     _dependencies.erase(std::type_index(typeid(T)));
        // }

        template <typename T>
        typename std::enable_if<std::is_base_of<AService, T>::value, T*>::type
        GetService()
        {
            //_getServiceCallback should not be null here.
            return reinterpret_cast<T*>(_getServiceCallback(std::type_index(typeid(T))));
        }

        //Only to be used as an example.
        // void DoEventLoop()
        // {
        //     while (eTaskGetState(NULL) != eTaskState::eDeleted)
        //         portYIELD();
        // }

    public:
        bool IsRunning()
        {
            _serviceMutex.lock();
            bool retVal = _taskHandle != nullptr;
            _serviceMutex.unlock();
            return retVal;
        }
    };
};
