#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <list>

namespace ReadieFur
{
    /*TODO: Change this to notify FreeRTOS tasks to run their own callbacks instead so that the task calling the dispatch
    method doesn't have to worry about having a large enough stack size for all of the callbacks.*/
    template <typename T>
    class Event
    {
    private:
        std::mutex _mutex;
        std::map<ulong, std::function<void(T)>> _callbacks;

    public:
        void Dispatch(T value)
        {
            _mutex.lock();

            for (auto &&kvp : _callbacks)
                kvp.second(value);

            _mutex.unlock();
        }

        ulong Add(std::function<void(T)> callback)
        {
            _mutex.lock();

            ulong id;
            do { id = millis(); }
            while (_callbacks.find(id) != _callbacks.end());

            _callbacks[id] = callback;

            _mutex.unlock();

            return id;
        }

        void Remove(ulong id)
        {
            _mutex.lock();
            _callbacks.erase(id);
            _mutex.unlock();
        }

        /// @return Returns the number of callbacks removed.
        size_t Remove(std::function<void(T)> callback)
        {
            std::list<ulong> callbacksToRemove;

            for (auto &&kvp : _callbacks)
                if (kvp.second == callback)
                    callbacksToRemove.push_back(kvp.first);

            for (auto &&id : callbacksToRemove)
                _callbacks.erase(id);

            return callbacksToRemove.size();
        }
    };
};
