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

namespace ReadieFur::Service
{
    class AService
    {
    friend class ServiceManager;
    private:
        std::mutex _serviceMutex;
        std::function<AService*(std::type_index)> _getServiceCallback = nullptr;
        std::unordered_set<std::type_index> _dependencies = {};
        TaskHandle_t* _taskHandle = nullptr;
        Event::AutoResetEvent _taskEvent;

        static void TaskWrapper(void* param)
        {
            AService* self = reinterpret_cast<AService*>(param);
            self->RunServiceImpl();

            if (eTaskGetState(NULL) != eTaskState::eDeleted)
            {
                //Consider the task as failed here, this occurs when the RunServiceImpl method returns before the task has been signalled for deletion.
                self->_serviceMutex.lock();
                vTaskDelete(NULL);
                self->_taskHandle = nullptr;
                self->_serviceMutex.unlock();
            }
            else
            {
                //Otherwise don't handle any cleanup here as it will be handled in the StopTask method.
            }

            self->_taskEvent.Set();
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

            #if configNUM_CORES > 1
            BaseType_t taskCreateResult;
            if (Core != -1 && Core >= 0 && Core < configNUM_CORES)
                taskCreateResult = xTaskCreatePinnedToCore(TaskWrapper, typeid(*this).name(), StackSize, this, TaskPriority, _taskHandle, Core);
            else
            #endif
                taskCreateResult = xTaskCreate(TaskWrapper, typeid(*this).name(), StackSize, this, TaskPriority, _taskHandle);

            _serviceMutex.unlock();

            return taskCreateResult == pdPASS ? EServiceResult::Ok : EServiceResult::Failed;
        }

        EServiceResult StopService(TickType_t timeout = portMAX_DELAY)
        {
            _serviceMutex.lock();
            
            if (_taskHandle == nullptr)
            {
                _serviceMutex.unlock();
                return EServiceResult::Ok;
            }

            vTaskDelete(_taskHandle);

            if (!_taskEvent.WaitOne(timeout))
            {
                _serviceMutex.unlock();
                return EServiceResult::Timeout;
            }
            _taskHandle = nullptr;

            _serviceMutex.unlock();
            return EServiceResult::Ok;
        }

    protected:
        virtual EServiceResult RunServiceImpl() = 0;

        uint TaskPriority = configMAX_PRIORITIES * 0.1;
        uint StackSize = configIDLE_TASK_STACK_SIZE;
        int Core = -1;

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
