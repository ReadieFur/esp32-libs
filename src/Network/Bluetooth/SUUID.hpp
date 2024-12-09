#pragma once

#include <esp_bt_defs.h>
#include <string.h>
#include <string>
#include "Logging.hpp"
#include "Helpers.h"

namespace ReadieFur::Network::Bluetooth
{
    struct SUUID : esp_bt_uuid_t
    {
    public:
        SUUID()
        {
            len = 0;
        }

        SUUID(uint16_t uuid16)
        {
            len = ESP_UUID_LEN_16;
            uuid.uuid16 = uuid16;
        }

        SUUID(uint32_t uuid32)
        {
            len = ESP_UUID_LEN_32;
            uuid.uuid32 = uuid32;
        }

        SUUID(uint8_t* uuid128)
        {
            len = ESP_UUID_LEN_128;
            memcpy(uuid.uuid128, uuid128, ESP_UUID_LEN_128);
        }

        SUUID(uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4, uint8_t c5, uint8_t c6, uint8_t c7, uint8_t c8,
            uint8_t c9, uint8_t c10, uint8_t c11, uint8_t c12, uint8_t c13, uint8_t c14, uint8_t c15, uint8_t c16)
        {
            len = ESP_UUID_LEN_128;
            uuid.uuid128[0] = c1;
            uuid.uuid128[1] = c2;
            uuid.uuid128[2] = c3;
            uuid.uuid128[3] = c4;
            uuid.uuid128[4] = c5;
            uuid.uuid128[5] = c6;
            uuid.uuid128[6] = c7;
            uuid.uuid128[7] = c8;
            uuid.uuid128[8] = c9;
            uuid.uuid128[9] = c10;
            uuid.uuid128[10] = c11;
            uuid.uuid128[11] = c12;
            uuid.uuid128[12] = c13;
            uuid.uuid128[13] = c14;
            uuid.uuid128[14] = c15;
            uuid.uuid128[15] = c16;
        }

        // SUUID(const char* uuidCStr)
        // {
        //     //Check for correct UUID format length (36 characters).
        //     if (strlen(uuidCStr) != 36)
        //     {
        //         LOGE(nameof(SUUID), "Invalid UUID length.");
        //         return;
        //     }

        //     std::string uuidStr = uuidCStr;

        //     //Remove dashes and validate.
        //     std::string stripped;
        //     for (size_t i = 0; i < uuidStr.length(); ++i)
        //     {
        //         if (uuidStr[i] == '-')
        //         {
        //             continue;
        //         }
        //         else if (!isxdigit(uuidStr[i]))
        //         {
        //             LOGE(nameof(SUUID), "Invalid UUID format.");
        //             return;
        //         }
        //         stripped += uuidStr[i];
        //     }

        //     //Ensure we have exactly 32 hex characters.
        //     if (stripped.length() != 32)
        //     {
        //         LOGE(nameof(SUUID), "Failed to parse UUID.");
        //         return;
        //     }

        //     try
        //     {
        //         //Parse hex pairs into bytes.
        //         for (size_t i = 0; i < 16; ++i)
        //         {
        //             std::string byteStr = stripped.substr(i * 2, 2);
        //             uuid.uuid128[i] = static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16));
        //         }
        //     }
        //     catch (const std::exception& e)
        //     {
        //         LOGE(nameof(SUUID), "Failed to parse UUID: %s", e.what());
        //         return;
        //     }
            
        //     len = ESP_UUID_LEN_128;
        // }

        bool IsValid() { return len != 0; }

        size_t Length() { return len; }

        void* Data()
        {
            switch (len)
            {
            case ESP_UUID_LEN_16:
                return (void*)&uuid.uuid16;
            case ESP_UUID_LEN_32:
                return (void*)&uuid.uuid32;
            case ESP_UUID_LEN_128:
                return (void*)&uuid.uuid128;
            default:
                return nullptr;
            }
        }

        // std::string ToString()
        // {
        //     if (!IsValid())
        //         return "Invalid UUID";

        //     switch (len)
        //     {
        //     case ESP_UUID_LEN_16:
        //     {
        //         char buffer[5];
        //         snprintf(buffer, sizeof(buffer), "%04x", uuid.uuid16);
        //         return std::string(buffer);
        //     }
        //     case ESP_UUID_LEN_32:
        //     {
        //         char buffer[9];
        //         snprintf(buffer, sizeof(buffer), "%08x", uuid.uuid32);
        //         return std::string(buffer);
        //     }
        //     case ESP_UUID_LEN_128:
        //     {
        //         char buffer[37]; //UUID string length is 36 + null terminator.
        //         snprintf(buffer, sizeof(buffer), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        //             uuid.uuid128[3], uuid.uuid128[2], uuid.uuid128[1], uuid.uuid128[0],  // First 4 bytes (a8f4000)
        //             uuid.uuid128[5], uuid.uuid128[4],                      // Next 2 bytes (000c)
        //             uuid.uuid128[7], uuid.uuid128[6],                      // Next 2 bytes (4084)
        //             uuid.uuid128[9], uuid.uuid128[8],                      // Next 2 bytes (fd40)
        //             uuid.uuid128[11], uuid.uuid128[10],                    // Next 2 bytes (4082)
        //             uuid.uuid128[15], uuid.uuid128[14], uuid.uuid128[13], uuid.uuid128[12] // Last 4 bytes (a8f44085)
        //         );
        //         return std::string(buffer);
        //     }
        //     default:
        //         return "Invalid UUID";
        //     }
        // }

        // operator size_t() const { return Length(); }

        // operator uint16_t() const { return uuid.uuid16; }

        // operator uint32_t() const { return uuid.uuid32; }

        // operator uint8_t*() const { return (uint8_t*)uuid.uuid128; }

        // operator void*() const { return Data(); }

        bool operator<(const SUUID& other) const
        {
            if (len != other.len)
                return len < other.len;

            switch (len)
            {
            case ESP_UUID_LEN_16:
                return uuid.uuid16 < other.uuid.uuid16;
            case ESP_UUID_LEN_32:
                return uuid.uuid32 < other.uuid.uuid32;
            case ESP_UUID_LEN_128:
                return memcmp(uuid.uuid128, other.uuid.uuid128, ESP_UUID_LEN_128) < 0;
            default:
                return false;
            }
        }

        bool operator==(const SUUID& other) const
        {
            if (len != other.len)
                return false;

            switch (len)
            {
            case ESP_UUID_LEN_16:
                return uuid.uuid16 == other.uuid.uuid16;
            case ESP_UUID_LEN_32:
                return uuid.uuid32 == other.uuid.uuid32;
            case ESP_UUID_LEN_128:
                return memcmp(uuid.uuid128, other.uuid.uuid128, ESP_UUID_LEN_128) == 0;
            default:
                return false;
            }
        }
    };
};
