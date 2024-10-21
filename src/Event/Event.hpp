#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <list>
#include <freertos/task.h>

namespace ReadieFur::Event
{
    /*TODO: Change this to notify FreeRTOS tasks to run their own callbacks instead so that the task calling the dispatch
    method doesn't have to worry about having a large enough stack size for all of the callbacks.*/
    template <typename T>
    class Event
    {
    private:
        std::mutex _mutex;
        std::map<TickType_t, std::function<void(T)>> _callbacks;

    public:
        void Dispatch(T value)
        {
            _mutex.lock();

            for (auto &&kvp : _callbacks)
                kvp.second(value);

            _mutex.unlock();
        }

        TickType_t Add(std::function<void(T)> callback)
        {
            _mutex.lock();

            TickType_t id;
            do { id = xTaskGetTickCount(); }
            while (_callbacks.find(id) != _callbacks.end());

            _callbacks[id] = callback;

            _mutex.unlock();

            return id;
        }

        void Remove(TickType_t id)
        {
            _mutex.lock();
            _callbacks.erase(id);
            _mutex.unlock();
        }

        /// @return Returns the number of callbacks removed.
        size_t Remove(std::function<void(T)> callback)
        {
            std::list<TickType_t> callbacksToRemove;

            for (auto &&kvp : _callbacks)
                if (kvp.second == callback)
                    callbacksToRemove.push_back(kvp.first);

            for (auto &&id : callbacksToRemove)
                _callbacks.erase(id);

            return callbacksToRemove.size();
        }
    };
};
