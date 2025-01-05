#pragma once

#include <esp_gatt_defs.h>
#include <mutex>
#include <esp_err.h>
#include <esp_gatts_api.h>
#include <Logging.hpp>
#include <Helpers.h>
#include <memory.h>
#include <functional>
#include <map>
#include <string.h>
#include "SUUID.hpp"

namespace ReadieFur::Network::Bluetooth
{
    static const uint16_t GATT_PRIMARY_SERVICE_UUID = ESP_GATT_UUID_PRI_SERVICE;
    static const uint16_t GATT_CHARACTERISTIC_DECLARATION_UUID = ESP_GATT_UUID_CHAR_DECLARE;
    static const uint8_t GATT_CHARACTERISTIC_PROP_READ = ESP_GATT_CHAR_PROP_BIT_READ;
    static const uint8_t GATT_CHARACTERISTIC_PROP_WRITE = ESP_GATT_CHAR_PROP_BIT_WRITE;
    static const uint8_t GATT_CHARACTERISTIC_PROP_READ_WRITE = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE;
    typedef std::function<esp_gatt_status_t(uint8_t* outValue, uint16_t* outLength)> TGattServerReadCallback; //Set the parameter names for IDE hints.
    typedef std::function<esp_gatt_status_t(uint8_t* inValue, uint16_t inLength)> TGattServerWriteCallback;

    //https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/bluedroid/ble/gatt_server/tutorial/Gatt_Server_Example_Walkthrough.md
    class GattServerService
    {
    private:
        struct SAttributeInfo
        {
            SUUID uuid;
            uint16_t permissions;
            void* value;
            uint16_t length;
            uint16_t maxLength;
            bool autoResponse;
            TGattServerReadCallback readCallback;
            TGattServerWriteCallback writeCallback;
        };

        std::mutex _mutex;
        bool _frozen = false;
        SUUID _serviceUUID;
        uint8_t _instanceId;
        std::vector<SAttributeInfo> _attributeInfos;
        size_t _uuidsSize = 0;
        SUUID* _uuids = nullptr;
        size_t _nativeAttributesSize = 0;
        esp_gatts_attr_db_t* _nativeAttributes = nullptr;
        uint16_t* _handleTable = nullptr;
        std::map<uint16_t, size_t> _handleMap;

        GattServerService() {}

        //https://www.gofakeit.com/funcs/uint32
        //https://www.rapidtables.com/convert/number/decimal-to-hex.html
        esp_err_t AddAttribute(
            SUUID uuid,
            uint16_t permissions,
            void* value,
            uint16_t length,
            uint16_t maxLength,
            bool autoResponse,
            TGattServerReadCallback readCallback,
            TGattServerWriteCallback writeCallback
        )
        {
            _mutex.lock();
            if (_frozen)
            {
                _mutex.unlock();
                return ESP_ERR_INVALID_STATE;
            }

            //Decleration.
            const uint8_t* declerationValue;
            if ((permissions & ESP_GATT_PERM_READ && permissions & ESP_GATT_PERM_WRITE) || (permissions & ESP_GATT_PERM_READ_ENCRYPTED && permissions & ESP_GATT_PERM_WRITE_ENCRYPTED))
                declerationValue = &GATT_CHARACTERISTIC_PROP_READ_WRITE;
            else if ((permissions & ESP_GATT_PERM_READ) || (permissions & ESP_GATT_PERM_READ_ENCRYPTED))
                declerationValue = &GATT_CHARACTERISTIC_PROP_READ;
            else if ((permissions & ESP_GATT_PERM_WRITE) || (permissions & ESP_GATT_PERM_WRITE_ENCRYPTED))
                declerationValue = &GATT_CHARACTERISTIC_PROP_WRITE;
            else
                declerationValue = 0;

            _attributeInfos.push_back(SAttributeInfo
            {
                .uuid = SUUID(GATT_CHARACTERISTIC_DECLARATION_UUID),
                .permissions = ESP_GATT_PERM_READ,
                .value = (void*)declerationValue,
                .length = sizeof(uint8_t),
                .maxLength = sizeof(uint8_t),
                .autoResponse = true,
                .readCallback = nullptr,
                .writeCallback = nullptr
            });

            //Value.
            _attributeInfos.push_back(SAttributeInfo
            {
                .uuid = uuid,
                .permissions = permissions,
                .value = value,
                .length = length,
                .maxLength = maxLength,
                .autoResponse = autoResponse,
                .readCallback = readCallback,
                .writeCallback = writeCallback
            });

            _mutex.unlock();
            return ESP_OK;
        }

        esp_err_t GenerateNativeAttributes()
        {
            size_t size = _attributeInfos.size();

            _uuids = (SUUID*)malloc(size * sizeof(SUUID));
            if (_uuids == nullptr)
            {
                LOGE(nameof(Bluetooth::GattServerService), "Failed to allocate memory for UUIDs.");
                return ESP_ERR_NO_MEM;
            }
            _uuidsSize = size;

            //Previously I was assigning to this directly with realloc however that caused pointer issues which broke old references, so now the native objects will be created once the object is frozen.
            _nativeAttributes = (esp_gatts_attr_db_t*)malloc(size * sizeof(esp_gatts_attr_db_t));
            if (_nativeAttributes == nullptr)
            {
                _uuidsSize = 0;
                free(_uuids);
                LOGE(nameof(Bluetooth::GattServerService), "Failed to allocate memory for attributes.");
                return ESP_ERR_NO_MEM;
            }
            _nativeAttributesSize = size;

            for (size_t i = 0; i < _attributeInfos.size(); i++)
            {
                LOGV(nameof(Bluetooth::GattServerService), "Generating attribute %d, UUID: %x", i, *(uint16_t*)_attributeInfos[i].uuid.Data());

                auto attributeInfo = _attributeInfos[i];
                _uuids[i] = attributeInfo.uuid;
                _nativeAttributes[i] =
                {
                    .attr_control = { .auto_rsp = (uint8_t)(attributeInfo.autoResponse ? ESP_GATT_AUTO_RSP : ESP_GATT_RSP_BY_APP) },
                    .att_desc =
                    {
                        .uuid_length = (uint16_t)_uuids[i].Length(),
                        .uuid_p = (uint8_t*)_uuids[i].Data(),
                        .perm = attributeInfo.permissions,
                        .max_length = attributeInfo.maxLength,
                        .length = attributeInfo.length,
                        .value = (uint8_t*)attributeInfo.value
                    }
                };
            }

            return ESP_OK;
        }

    public:
        GattServerService(SUUID uuid, uint8_t instanceId) : _serviceUUID(uuid), _instanceId(instanceId)
        {
            _attributeInfos.push_back(SAttributeInfo
            {
                .uuid = SUUID(GATT_PRIMARY_SERVICE_UUID),
                .permissions = ESP_GATT_PERM_READ,
                .value = (void*)_serviceUUID.Data(),
                .length = sizeof(uint16_t),
                .maxLength = sizeof(uint16_t),
                .autoResponse = true,
                .readCallback = nullptr,
                .writeCallback = nullptr
            });
        }

        ~GattServerService()
        {
            _mutex.lock();

            if (_handleTable != nullptr)
            {
                delete[] _handleTable;
                _handleTable = nullptr;
            }

            if (_nativeAttributes != nullptr)
            {
                free(_nativeAttributes);
                _nativeAttributes = nullptr;
            }

            if (_uuids != nullptr)
            {
                free(_uuids);
                _uuids = nullptr;
            }

            _mutex.unlock();
        }

        #pragma Region Auto responders
        esp_err_t AddAttribute(
            SUUID uuid,
            uint16_t permissions,
            void* value,
            uint16_t length,
            uint16_t maxLength
        )
        {
            return AddAttribute(uuid, permissions, value, length, maxLength, true, nullptr, nullptr);
        }

        #pragma endregion

        #pragma region Manual responders
        esp_err_t AddAttribute(
            SUUID uuid,
            uint16_t permissions,
            TGattServerReadCallback readCallback = nullptr,
            TGattServerWriteCallback writeCallback = nullptr
        )
        {
            return AddAttribute(uuid, permissions, NULL, 0, 0, false, readCallback, writeCallback);
        }
        #pragma endregion

        esp_err_t AddAttribute(esp_gatts_attr_db_t attribute, TGattServerReadCallback readCallback = nullptr, TGattServerWriteCallback writeCallback = nullptr)
        {
            _mutex.lock();
            if (_frozen)
            {
                _mutex.unlock();
                return ESP_ERR_INVALID_STATE;
            }

            SUUID uuid;
            switch (attribute.att_desc.uuid_length)
            {
            case ESP_UUID_LEN_16:
                uuid = SUUID(*(uint16_t*)attribute.att_desc.uuid_p);
                break;
            case ESP_UUID_LEN_32:
                uuid = SUUID(*(uint32_t*)attribute.att_desc.uuid_p);
                break;
            case ESP_UUID_LEN_128:
                uuid = SUUID((uint8_t*)attribute.att_desc.uuid_p);
                break;
            default:
                _mutex.unlock();
                return ESP_ERR_INVALID_ARG;
            }

            _attributeInfos.push_back(SAttributeInfo
            {
                .uuid = uuid,
                .permissions = attribute.att_desc.perm,
                .value = attribute.att_desc.value,
                .length = attribute.att_desc.length,
                .maxLength = attribute.att_desc.max_length,
                .autoResponse = attribute.attr_control.auto_rsp == ESP_GATT_AUTO_RSP,
                .readCallback = readCallback,
                .writeCallback = writeCallback
            });

            _mutex.unlock();
            return ESP_OK;
        }

        uint16_t GetAttributeHandle(SUUID uuid)
        {
            _mutex.lock();
            if (!_frozen)
            {
                _mutex.unlock();
                return 0;
            }

            auto attributeIndex = std::find_if(_attributeInfos.begin(), _attributeInfos.end(), [uuid](SAttributeInfo info) { return info.uuid == uuid; });
            if (attributeIndex == _attributeInfos.end())
            {
                _mutex.unlock();
                return 0;
            }

            auto handleIndex = _handleMap.find(attributeIndex - _attributeInfos.begin());
            if (handleIndex == _handleMap.end())
            {
                _mutex.unlock();
                return 0;
            }

            _mutex.unlock();
            return handleIndex->first;
        }

        void ProcessServerEvent(esp_gatts_cb_event_t event, esp_gatt_if_t gattsIf, esp_ble_gatts_cb_param_t* param)
        {
            // LOGV(nameof(Bluetooth::GattServerService), "GATT_SVC_EVT: %d", event);

            _mutex.lock();

            switch (event)
            {
            case ESP_GATTS_REG_EVT:
            {
                //Freeze the object and register it.
                if (_frozen)
                    break;

                esp_err_t err;

                if ((err = GenerateNativeAttributes()) != ESP_OK)
                {
                    LOGE(nameof(Bluetooth::GattServerService), "Failed to generate native attributes: %s", esp_err_to_name(err));
                    break;
                }

                _handleTable = new uint16_t[_nativeAttributesSize];
                if (_handleTable == nullptr)
                {
                    free(_uuids);
                    _uuids = nullptr;
                    _uuidsSize = 0;
                    free(_nativeAttributes);
                    _nativeAttributes = nullptr;
                    _nativeAttributesSize = 0;
                    _mutex.unlock();
                    break;
                }

                if ((err = esp_ble_gatts_create_attr_tab(_nativeAttributes, gattsIf, _nativeAttributesSize, _instanceId)) != ESP_OK)
                {
                    free(_uuids);
                    _uuids = nullptr;
                    _uuidsSize = 0;
                    free(_nativeAttributes);
                    _nativeAttributes = nullptr;
                    _nativeAttributesSize = 0;
                    LOGE(nameof(Bluetooth::GattServerService), "Register attribute table failed: %s", esp_err_to_name(err));
                    break;
                }
                else
                {
                    LOGV(nameof(Bluetooth::GattServerService), "Registered %d attributes.", _nativeAttributesSize);
                }

                //Free memory by clearing the wrapped attribute vector.
                // _attributeInfos.clear(); //Currently needed for references to the original data.

                _frozen = true;
                break;
            }
            case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            {
                if (!_frozen)
                    break;

                if (param->add_attr_tab.svc_inst_id != _instanceId)
                    break;

                // LOGD(nameof(Bluetooth::GattServerServiceDatabase), "Attribute number handle: %i", param->add_attr_tab.num_handle);
                if (param->create.status != ESP_GATT_OK)
                {
                    LOGE(nameof(Bluetooth::GattServerService), "Create attribute table failed: %x", param->create.status);
                    break;
                }

                if (param->add_attr_tab.num_handle != _nativeAttributesSize)
                {
                    LOGE(nameof(Bluetooth::GattServerService), "Create attribute table abnormally, num_handle (%d) doesn't equal to _attributes.size(%d)", param->add_attr_tab.num_handle, _nativeAttributesSize);
                    break;
                }

                for (size_t i = 0; i < param->add_attr_tab.num_handle; i++)
                    _handleMap[param->add_attr_tab.handles[i]] = i;

                memcpy(_handleTable, param->add_attr_tab.handles, _nativeAttributesSize);
                esp_err_t err = esp_ble_gatts_start_service(_handleTable[0]);
                if (err != ESP_OK)
                    LOGE(nameof(Bluetooth::GattServerService), "Start service failed: %s", esp_err_to_name(err));
                else
                    LOGV(nameof(Bluetooth::GattServerService), "Started service: %ld", _handleTable[0]);
                break;
            }
            case ESP_GATTS_READ_EVT:
            {
                if (!_frozen)
                {
                    esp_ble_gatts_send_response(gattsIf, param->read.conn_id, param->read.trans_id, ESP_GATT_NO_RESOURCES, nullptr);
                    break;
                }

                SAttributeInfo attributeInfo = _attributeInfos[_handleMap[param->read.handle]];

                if (attributeInfo.autoResponse)
                {
                    //If auto response is set, the BLE stack will handle getting and setting the balue, so just return here.
                    break;
                }

                esp_gatt_rsp_t rsp;
                memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
                rsp.attr_value.handle = param->read.handle;

                esp_gatt_status_t status;
                if (attributeInfo.readCallback != nullptr)
                {
                    status = attributeInfo.readCallback(rsp.attr_value.value, &rsp.attr_value.len);
                }
                else
                {
                    //If the write callback is not set, return the value as is.
                    rsp.attr_value.len = attributeInfo.length;
                    memcpy(rsp.attr_value.value, attributeInfo.value, rsp.attr_value.len);
                    status = ESP_GATT_OK;
                }
                esp_ble_gatts_send_response(gattsIf, param->read.conn_id, param->read.trans_id, status, &rsp);
                break;
            }
            case ESP_GATTS_WRITE_EVT:
            {
                if (!_frozen)
                {
                    esp_ble_gatts_send_response(gattsIf, param->write.conn_id, param->write.trans_id, ESP_GATT_NO_RESOURCES, nullptr);
                    break;
                }

                SAttributeInfo attributeInfo = _attributeInfos[_handleMap[param->read.handle]];

                if (attributeInfo.autoResponse)
                    break;

                esp_gatt_status_t status;
                if (attributeInfo.writeCallback != nullptr)
                {
                    status = attributeInfo.writeCallback(param->write.value, param->write.len);
                }
                else
                {
                    ////If the write callback is not set, attempt to write to the value directly.
                    //The above wont work as this wrapper code assumes value is null when a write callback is set.
                    //Instead send an error response.
                    status = ESP_GATT_WRITE_NOT_PERMIT;
                }
                esp_ble_gatts_send_response(gattsIf, param->write.conn_id, param->write.trans_id, status, nullptr);
                break;
            }
            default:
                break;
            }

            _mutex.unlock();
        }
    };
};
