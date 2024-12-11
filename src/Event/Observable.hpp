#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <vector>
#include <mutex>
#include <map>
#include <esp_err.h>

namespace ReadieFur::Event
{
    template <typename T>
    class Observable
    {
    public:
        typedef uint32_t TObservableHandle;

    private:
        //Advanced setup to allow for unsyncnonused event groups.
        struct SGroupInfo
        {
            EventGroupHandle_t group;
            std::vector<TObservableHandle> handles;
        };

        std::mutex _mutex;
        std::map<size_t, SGroupInfo> _groups;
        T _value;

    public:
        Observable()
        {
            _groups[0] = SGroupInfo {
                .group = xEventGroupCreate(),
                .handles = { (1 << 0) }
            };
        }

        ~Observable()
        {
            for (auto &&group : _groups)
                vEventGroupDelete(group.second.group);
        }

        T Get() const
        {
            return _value;
        }

        esp_err_t Register(TObservableHandle& outHandle)
        {
            std::lock_guard<std::mutex> lock(_mutex);

            //Find the first free handle.
            for (auto &&group : _groups)
            {
                size_t groupId = group.first;

                //Check if the group has any free handles.
                if (group.second.handles.size() >= 32)
                    continue;
                
                //Find the first free handle.
                for (uint32_t j = 0; j < 32; j++)
                {
                    if (std::find(group.second.handles.begin(), group.second.handles.end(), j) != group.second.handles.end())
                        continue;

                    outHandle = groupId * 32 + j;
                    group.second.handles.push_back(outHandle);
                    return ESP_OK;
                }
            }

            //If we reach this point then we need to create a new group.
            size_t groupId;
            for (groupId = 0; groupId < UINT8_MAX; groupId++)
                if (_groups.find(groupId) == _groups.end())
                    break;
            if (groupId == UINT8_MAX)
                return ESP_ERR_NO_MEM;

            _groups[groupId] = SGroupInfo
            {
                .group = xEventGroupCreate(),
                .handles = { (1 << 0) }
            };
            outHandle = groupId * 32;
            return ESP_OK;
        }

        esp_err_t Unregister(TObservableHandle& handle)
        {
            std::lock_guard<std::mutex> lock(_mutex);

            //Prevent removal of the base handle.
            if (handle == 0)
                return ESP_ERR_INVALID_ARG;

            size_t groupId = handle / 32;
            auto group = _groups.find(groupId);
            if (group == _groups.end())
                return ESP_ERR_NOT_FOUND;

            uint32_t index = handle % 32;
            if (std::find(group->second.handles.begin(), group->second.handles.end(), index) == group->second.handles.end())
                return ESP_ERR_NOT_FOUND;

            //Set the bits incase anything is currently waiting on this handle, helps prevent a deadlock, though this shouldn't be called if something is currently waiting on this handle.
            xEventGroupSetBits(group->second.group, (1 << index));

            group->second.handles.erase(std::remove(group->second.handles.begin(), group->second.handles.end(), index), group->second.handles.end());

            //If the group is empty, delete it.
            if (group->second.handles.size() == 0)
            {
                vEventGroupDelete(group->second.group);
                _groups.erase(groupId);
            }

            handle = 0;
            return ESP_OK;
        }

        void Set(T value)
        {
            _value = value;
            
            for (auto &&group : _groups)
            {
                EventGroupHandle_t eventGroup = group.second.group;
                EventBits_t bits = 0;
                for (auto &&handle : group.second.handles)
                    bits |= (1 << handle);
                xEventGroupSetBits(eventGroup, bits);
            }
        }

        void SetFromISR(T value, BaseType_t* higherPriorityTaskWoken)
        {
            _value = value;

            for (auto &&group : _groups)
            {
                EventGroupHandle_t eventGroup = group.second.group;
                EventBits_t bits = 0;
                for (auto &&handle : group.second.handles)
                    bits |= (1 << handle);
                xEventGroupSetBitsFromISR(eventGroup, bits, higherPriorityTaskWoken);
            }
        }

        esp_err_t WaitOne(TickType_t timeout = portMAX_DELAY)
        {
            return xEventGroupWaitBits(
                _groups[0].group, //The event group being tested.
                (1 << 0), //The bits within the event group to wait for.
                pdTRUE, //The bits should be cleared before returning.
                pdFALSE, //Don't wait for all bits, just the one specified.
                timeout //Timeout.
            ) == (1 << 0) ? ESP_OK : ESP_ERR_TIMEOUT;
        }

        esp_err_t WaitOne(TObservableHandle handle, TickType_t timeout = portMAX_DELAY)
        {
            size_t groupId = handle / 32;
            auto group = _groups.find(groupId);
            if (group == _groups.end())
                return ESP_ERR_NOT_FOUND;

            uint32_t index = handle % 32;
            if (std::find(group->second.handles.begin(), group->second.handles.end(), index) == group->second.handles.end())
                return ESP_ERR_NOT_FOUND;

            return xEventGroupWaitBits(
                group->second.group, //The event group being tested.
                (1 << index), //The bits within the event group to wait for.
                pdTRUE, //The bits should be cleared before returning.
                pdFALSE, //Don't wait for all bits, just the one specified.
                timeout //Timeout.
            ) == (1 << index) ? ESP_OK : ESP_ERR_TIMEOUT;
        }
    };
};
