#pragma once

#include <stdint.h>
#include <esp_gattc_api.h>
#include <functional>

namespace ReadieFur::Network::Bluetooth
{
    struct SGattClientProfile
    {
        uint16_t appId;
        std::function<void(esp_gattc_cb_event_t, esp_gatt_if_t, esp_ble_gattc_cb_param_t*)> gattClientCallback = nullptr;
        uint16_t gattcIf = ESP_GATT_IF_NONE;
        uint16_t connectionId = 0;
    };
};
