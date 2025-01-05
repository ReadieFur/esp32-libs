#pragma once

#include <stdint.h>
#include <esp_gatts_api.h>
#include <functional>

namespace ReadieFur::Network::Bluetooth
{
    struct SGattServerProfile
    {
        uint16_t appId;
        std::function<void(esp_gatts_cb_event_t, esp_gatt_if_t, esp_ble_gatts_cb_param_t*)> gattServerCallback = nullptr;
        uint16_t gattsIf = ESP_GATT_IF_NONE;
        uint16_t connectionId = 0;
    };
};
