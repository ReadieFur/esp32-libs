#pragma once

#include <nvs_flash.h>
#include <esp_bt.h>
#include <esp_gap_ble_api.h>
#include <esp_gatts_api.h>
#include <esp_gattc_api.h>
#include <esp_gatt_defs.h>
#include <esp_bt_main.h>
#include <esp_gatt_common_api.h>
#include "Logging.hpp"
#include <map>
#include <functional>
#include <freertos/semphr.h>
#include "SGattServerProfile.h"
#include "SGattClientProfile.h"
#include <mutex>
#include <algorithm>
#include <esp_err.h>

#define ADV_CONFIG_FLAG                           (1 << 0)
#define SCAN_RSP_CONFIG_FLAG                      (1 << 1)

#define _BT_ESP_CHECK(func, msg) do {                       \
        esp_err_t res = func;                               \
        if (res != ESP_OK) {                                \
            LOGE(nameof(Bluetooth::BLE), msg ": %s", esp_err_to_name(res));  \
            return res;                                     \
        }                                                   \
    } while (0)

namespace ReadieFur::Network::Bluetooth
{
    //https://github.com/espressif/esp-idf/blob/6fe853a2c73437f74c0e6e79f9b15db68b231d32/examples/bluetooth/bluedroid/ble/gatt_security_server/tutorial/Gatt_Security_Server_Example_Walkthrough.md
    class BLE
    {
    private:
        static bool _initialized;
        static uint8_t _manufacturer[16];
        static uint8_t _serviceUuid[16];
        static esp_ble_adv_data_t _advertisingConfig;
        static esp_ble_adv_data_t _rspConfig;
        static esp_ble_adv_params_t _advertisingParams;
        static const char* _deviceName;
        static uint32_t _passkey;
        static std::mutex _mutex;
        static uint8_t _advConfigDone;
        static std::vector<SGattServerProfile*> _serverProfiles;
        static std::vector<SGattClientProfile*> _clientProfiles;

        static const char* EspKeyTypeToStr(esp_ble_key_type_t keyType)
        {
            switch(keyType)
            {
                case ESP_LE_KEY_NONE:
                    return "ESP_LE_KEY_NONE";
                    break;
                case ESP_LE_KEY_PENC:
                    return "ESP_LE_KEY_PENC";
                case ESP_LE_KEY_PID:
                    return "ESP_LE_KEY_PID";
                case ESP_LE_KEY_PCSRK:
                    return "ESP_LE_KEY_PCSRK";
                case ESP_LE_KEY_PLK:
                    return "ESP_LE_KEY_PLK";
                case ESP_LE_KEY_LLK:
                    return "ESP_LE_KEY_LLK";
                case ESP_LE_KEY_LENC:
                    return "ESP_LE_KEY_LENC";
                case ESP_LE_KEY_LID:
                    return "ESP_LE_KEY_LID";
                case ESP_LE_KEY_LCSRK:
                    return "ESP_LE_KEY_LCSRK";
                default:
                    return "INVALID BLE KEY TYPE";
            }
        }

        static const char* EspAuthReqToStr(esp_ble_auth_req_t authReq)
        {
            switch(authReq)
            {
                case ESP_LE_AUTH_NO_BOND:
                    return "ESP_LE_AUTH_NO_BOND";
                case ESP_LE_AUTH_BOND:
                    return "ESP_LE_AUTH_BOND";
                case ESP_LE_AUTH_REQ_MITM:
                    return "ESP_LE_AUTH_REQ_MITM";
                case ESP_LE_AUTH_REQ_BOND_MITM:
                    return "ESP_LE_AUTH_REQ_BOND_MITM";
                case ESP_LE_AUTH_REQ_SC_ONLY:
                    return "ESP_LE_AUTH_REQ_SC_ONLY";
                case ESP_LE_AUTH_REQ_SC_BOND:
                    return "ESP_LE_AUTH_REQ_SC_BOND";
                case ESP_LE_AUTH_REQ_SC_MITM:
                    return "ESP_LE_AUTH_REQ_SC_MITM";
                case ESP_LE_AUTH_REQ_SC_MITM_BOND:
                    return "ESP_LE_AUTH_REQ_SC_MITM_BOND";
                default:
                    return "INVALID BLE AUTH REQ";
            }
        }

        static void ShowBondedDevices()
        {
            int devNum = esp_ble_get_bond_device_num();

            esp_ble_bond_dev_t *dev_list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * devNum);
            esp_ble_get_bond_device_list(&devNum, dev_list);
            LOGI(nameof(Bluetooth::BLE), "Bonded devices number: %d", devNum);

            LOGI(nameof(Bluetooth::BLE), "Bonded devices list: %d", devNum);
            for (int i = 0; i < devNum; i++)
                esp_log_buffer_hex(nameof(Bluetooth::BLE), (void*)dev_list[i].bd_addr, sizeof(esp_bd_addr_t));

            free(dev_list);
        }

        static void GattServerEventHandler(esp_gatts_cb_event_t event, esp_gatt_if_t gattsIf, esp_ble_gatts_cb_param_t* param)
        {
            LOGV(nameof(Bluetooth::BLE), "GATTS_EVT: %d", event);

            switch (event)
            {
            case ESP_GATTS_REG_EVT: //If event is register event, store the gattsIf for each profile.
            {
                if (param->reg.status != ESP_GATT_OK)
                {
                    LOGE(nameof(Bluetooth::BLE), "Register server app failed. Invalid status, app_id %04x, status %d", param->reg.app_id, param->reg.status);
                    return;
                }

                esp_ble_gap_set_device_name(_deviceName);
                esp_ble_gap_config_local_privacy(true);

                auto profile = FindServerProfile(param->reg.app_id);
                if (profile == _serverProfiles.end())
                {
                    LOGE(nameof(Bluetooth::BLE), "Register server app failed. AppId not found: %d", param->reg.app_id);
                    return;
                }

                (*profile)->gattsIf = gattsIf;
                break;
            }
            case ESP_GATTS_CONNECT_EVT:
            {
                //Start security connect with peer device when receive the connect event sent by the master.
                esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM);
                
                for (auto &&profile : _serverProfiles)
                    profile->connectionId = param->connect.conn_id;

                break;
            }
            case ESP_GATTS_DISCONNECT_EVT: //Start advertising again when missing the connect.
            {
                LOGD(nameof(Bluetooth::API), "Disconnect reason: 0x%x", param->disconnect.reason);
                esp_ble_gap_start_advertising(&_advertisingParams);

                for (auto &&profile : _serverProfiles)
                    profile->connectionId = 0;

                break;
            }
            default:
                break;
            }

            for (int i = 0; i < _serverProfiles.size(); i++)
            {
                auto profile = _serverProfiles.at(i);
                //ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function.
                if ((gattsIf == ESP_GATT_IF_NONE || gattsIf == (*profile).gattsIf) && (*profile).gattServerCallback != nullptr)
                    (*profile).gattServerCallback(event, gattsIf, param);
            }
        }

        static void GattClientEventHandler(esp_gattc_cb_event_t event, esp_gatt_if_t gattcIf, esp_ble_gattc_cb_param_t* param)
        {
            LOGV(nameof(Bluetooth::BLE), "GATTC_EVT: %d", event);

            switch (event)
            {
            case ESP_GATTC_REG_EVT:
            {
                if (param->reg.status != ESP_GATT_OK)
                {
                    LOGE(nameof(Bluetooth::BLE), "Register client app failed. Invalid status, app_id %04x, status %d", param->reg.app_id, param->reg.status);
                    return;
                }

                auto profile = FindClientProfile(param->reg.app_id);
                if (profile == _clientProfiles.end())
                {
                    LOGE(nameof(Bluetooth::BLE), "Register client app failed. AppId not found: %d", param->reg.app_id);
                    return;
                }
                (*profile)->gattcIf = gattcIf;
                break;
            }
            default:
                break;
            }

            for (int i = 0; i < _clientProfiles.size(); i++)
            {
                auto profile = _clientProfiles.at(i);
                //ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function.
                if ((gattcIf == ESP_GATT_IF_NONE || gattcIf == (*profile).gattcIf) && (*profile).gattClientCallback != nullptr)
                    (*profile).gattClientCallback(event, gattcIf, param);
            }
        }

        static void GapEventHandler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param)
        {
            LOGV(nameof(Bluetooth::BLE), "GAP_EVT: %d", event);

            switch (event)
            {
            case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            {
                _advConfigDone &= (~SCAN_RSP_CONFIG_FLAG);
                if (_advConfigDone == 0)
                    esp_ble_gap_start_advertising(&_advertisingParams);
                break;
            }
            case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            {
                _advConfigDone &= (~ADV_CONFIG_FLAG);
                if (_advConfigDone == 0)
                    esp_ble_gap_start_advertising(&_advertisingParams);
                break;
            }
            case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            {
                //advertising start complete event to indicate advertising start successfully or failed
                if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
                    LOGE(nameof(Bluetooth::BLE), "Advertising start failed: %d", param->adv_start_cmpl.status);
                else
                    LOGD(nameof(Bluetooth::BLE), "Advertising start success.");
                break;
            }
            case ESP_GAP_BLE_PASSKEY_REQ_EVT:
            {
                /* Call the following function to input the passkey which is displayed on the remote device */
                // esp_ble_passkey_reply(heart_rate_profile_tab[HEART_PROFILE_APP_IDX].remote_bda, true, 0x00);
                esp_ble_passkey_reply(param->ble_security.ble_req.bd_addr, true, _passkey);
                break;
            }
            case ESP_GAP_BLE_OOB_REQ_EVT:
            {
                uint8_t tk[16] = {1}; //If you paired with OOB, both devices need to use the same tk
                esp_ble_oob_req_reply(param->ble_security.ble_req.bd_addr, tk, sizeof(tk));
                break;
            }
            case ESP_GAP_BLE_NC_REQ_EVT:
            {
                /* The app will receive this evt when the IO has DisplayYesNO capability and the peer device IO also has DisplayYesNo capability.
                show the passkey number to the user to confirm it with the number displayed by peer device. */
                esp_ble_confirm_reply(param->ble_security.ble_req.bd_addr, true);
                // LOGI(nameof(Bluetooth::BLE), "ESP_GAP_BLE_NC_REQ_EVT, the passkey Notify number: %lu", param->ble_security.key_notif.passkey);
                break;
            }
            case ESP_GAP_BLE_SEC_REQ_EVT:
            {
                /* send the positive(true) security response to the peer device to accept the security request.
                If not accept the security request, should send the security response with negative(false) accept value*/
                esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
                break;
            }
            // case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:  //The app will receive this evt when the IO  has Output capability and the peer device IO has Input capability.
            // {
            //     //Show the passkey number to the user to input it in the peer device.
            //     LOGI(nameof(Bluetooth::BLE), "The passkey Notify number: %06lu", param->ble_security.key_notif.passkey);
            //     break;
            // }
            // case ESP_GAP_BLE_KEY_EVT:
            // {
            //     //Shows the ble key info share with peer device to the user.
            //     LOGD(nameof(Bluetooth::BLE), "Key type: %s", EspKeyTypeToStr(param->ble_security.ble_key.key_type));
            //     break;
            // }
            case ESP_GAP_BLE_AUTH_CMPL_EVT:
            {
                esp_bd_addr_t bdAddr;
                // esp_ble_gap_set_rand_addr(bd_addr);
                memcpy(bdAddr, param->ble_security.auth_cmpl.bd_addr, sizeof(esp_bd_addr_t));
                LOGI(nameof(Bluetooth::BLE), "Remote BD_ADDR: %08x%04x",
                    (bdAddr[0] << 24) + (bdAddr[1] << 16) + (bdAddr[2] << 8) + bdAddr[3],
                    (bdAddr[4] << 8) + bdAddr[5]);
                LOGI(nameof(Bluetooth::BLE), "Address type: %ul", param->ble_security.auth_cmpl.addr_type);
                LOGI(nameof(Bluetooth::BLE), "Pair status: %s",param->ble_security.auth_cmpl.success ? "success" : "fail");
                if(!param->ble_security.auth_cmpl.success)
                    LOGE(nameof(Bluetooth::BLE), "Fail reason: %d", param->ble_security.auth_cmpl.fail_reason);
                else
                    LOGI(nameof(Bluetooth::BLE), "Auth mode: %s", EspAuthReqToStr(param->ble_security.auth_cmpl.auth_mode));
                // ShowBondedDevices();
                break;
            }
            // case ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT:
            // {
            //     LOGD(nameof(Bluetooth::BLE), "ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT status: %d", param->remove_bond_dev_cmpl.status);
            //     LOGI(nameof(Bluetooth::BLE), "ESP_GAP_BLE_REMOVE_BOND_DEV");
            //     LOGI(nameof(Bluetooth::BLE), "-----ESP_GAP_BLE_REMOVE_BOND_DEV----");
            //     esp_log_buffer_hex(nameof(Bluetooth::BLE), (void *)param->remove_bond_dev_cmpl.bd_addr, sizeof(esp_bd_addr_t));
            //     LOGI(nameof(Bluetooth::BLE), "------------------------------------");
            //     break;
            // }
            case ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT:
            {
                if (param->local_privacy_cmpl.status != ESP_BT_STATUS_SUCCESS)
                {
                    LOGE(nameof(Bluetooth::BLE), "Config local privacy failed: %d", param->local_privacy_cmpl.status);
                    break;
                }

                esp_err_t ret = esp_ble_gap_config_adv_data(&_advertisingConfig);
                if (ret != ESP_OK)
                    LOGE(nameof(Bluetooth::BLE), "config adv data failed: %d", ret);
                else
                    _advConfigDone |= ADV_CONFIG_FLAG;

                ret = esp_ble_gap_config_adv_data(&_rspConfig);
                if (ret != ESP_OK)
                    LOGE(nameof(Bluetooth::BLE), "config adv data failed, error code: %d", ret);
                else
                    _advConfigDone |= SCAN_RSP_CONFIG_FLAG;

                break;
            }
            default:
                break;
            }
        }

        static std::vector<SGattServerProfile*>::iterator FindServerProfile(uint16_t appId)
        {
            return std::find_if(_serverProfiles.begin(), _serverProfiles.end(), [appId](auto p){ return appId == p->appId; });
        }

        static std::vector<SGattClientProfile*>::iterator FindClientProfile(uint16_t appId)
        {
            return std::find_if(_clientProfiles.begin(), _clientProfiles.end(), [appId](auto p){ return appId == p->appId; });
        }

    public:
        static esp_err_t Init(const char* deviceName, uint32_t passkey)
        {
            _mutex.lock();
            if (_initialized)
            {
                _mutex.unlock();
                return ESP_ERR_INVALID_STATE;
            }

            _deviceName = deviceName;
            _passkey = passkey;

            // err = nvs_flash_init();
            // if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
            // {
            //     nvs_flash_erase();
            //     err = nvs_flash_init();
            // }
            // if (err != ESP_OK)
            //     return;

            //Set to BLE only (disable BT Classic).
            _BT_ESP_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT), "Failed to release BT mode");

            //Initialize BLE controller.
            esp_bt_controller_config_t btCfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
            _BT_ESP_CHECK(esp_bt_controller_init(&btCfg), "Initialize BLE controller failed");
            _BT_ESP_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE), "Enable BLE controller failed");
            _BT_ESP_CHECK(esp_bluedroid_init(), "Initialize bluetooth failed");
            _BT_ESP_CHECK(esp_bluedroid_enable(), "Enable bluetooth failed");
            // _BT_ESP_CHECK(esp_ble_gap_set_device_name(_deviceName), "Failed to set device name");
            // _BT_ESP_CHECK(esp_ble_gap_config_local_privacy(true), "Failed to set privacy setting");
            _BT_ESP_CHECK(esp_ble_gatt_set_local_mtu(200), "Failed to set MTU");

            //Configure callbacks.
            _BT_ESP_CHECK(esp_ble_gap_register_callback(GapEventHandler), "GAP register callback failed");
            _BT_ESP_CHECK(esp_ble_gatts_register_callback(GattServerEventHandler), "GATT server callback registration failed");
            _BT_ESP_CHECK(esp_ble_gattc_register_callback(GattClientEventHandler), "GATT client callback registration failed");

            //Set security parameters.
            esp_ble_auth_req_t authReq = ESP_LE_AUTH_REQ_SC_MITM_BOND; //Bonding with client device after authentication.
            esp_ble_io_cap_t ioCap = ESP_IO_CAP_OUT; //Require passkey from user.
            uint8_t keySizeMin = 4; //The passkey size should be 4~16 bytes.
            uint8_t keySizeMax = 16;
            uint8_t initKey = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
            uint8_t rspKey = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
            uint8_t authOption = ESP_BLE_ONLY_ACCEPT_SPECIFIED_AUTH_DISABLE;
            uint8_t oobSupport = ESP_BLE_OOB_DISABLE;
            if (_passkey != 0)
                esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &_passkey, sizeof(uint32_t));
            esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &authReq, sizeof(uint8_t));
            esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &ioCap, sizeof(uint8_t));
            esp_ble_gap_set_security_param(ESP_BLE_SM_MIN_KEY_SIZE, &keySizeMin, sizeof(uint8_t));
            esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &keySizeMax, sizeof(uint8_t));
            esp_ble_gap_set_security_param(ESP_BLE_SM_ONLY_ACCEPT_SPECIFIED_SEC_AUTH, &authOption, sizeof(uint8_t));
            esp_ble_gap_set_security_param(ESP_BLE_SM_OOB_SUPPORT, &oobSupport, sizeof(uint8_t));
            esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &initKey, sizeof(uint8_t));
            esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rspKey, sizeof(uint8_t));

            _initialized = true;
            _mutex.unlock();
            return ESP_OK;
        }

        static esp_err_t Deinit()
        {
            _mutex.lock();
            if (!_initialized)
            {
                _mutex.unlock();
                return ESP_ERR_INVALID_STATE;
            }

            for (auto &&profile : _serverProfiles)
                if (profile->gattsIf != ESP_GATT_IF_NONE)
                    esp_ble_gatts_app_unregister(profile->gattsIf);
            _serverProfiles.clear();

            for (auto &&profile : _clientProfiles)
                if (profile->gattcIf != ESP_GATT_IF_NONE)
                    esp_ble_gattc_app_unregister(profile->gattcIf);
            _clientProfiles.clear();

            esp_err_t err;

            if ((err = esp_bluedroid_disable()) != ESP_OK)
                LOGE(nameof(Bluetooth::BLE), "Disable bluetooth failed: %d", err);

            if ((err = esp_bluedroid_deinit()) != ESP_OK)
                LOGE(nameof(Bluetooth::BLE), "Deinit bluetooth failed: %d", err);

            if ((err = esp_bt_controller_disable()) != ESP_OK)
                LOGE(nameof(Bluetooth::BLE), "Disable BLE controller failed: %d", err);

            if ((err = esp_bt_controller_deinit()) != ESP_OK)
                LOGE(nameof(Bluetooth::BLE), "Deinit BLE controller failed: %d", err);

            _initialized = false;
            _mutex.unlock();
            return err;
        }

        static void SetDeviceName(const char* deviceName)
        {
            _deviceName = deviceName;
            esp_ble_gap_set_device_name(_deviceName);
        }

        static void SetPin(uint32_t passkey)
        {
            _passkey = passkey;
            if (_passkey != 0)
                esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &_passkey, sizeof(uint32_t));
            else
                esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, NULL, 0);
        }

        static bool IsInitialized()
        {
            return _initialized;
        }

        static esp_err_t RegisterServerApp(SGattServerProfile* profile)
        {
            _mutex.lock();
            if (!_initialized)
            {
                _mutex.unlock();
                return ESP_ERR_INVALID_STATE;
            }

            if (FindServerProfile(profile->appId) != _serverProfiles.end())
                return ESP_ERR_INVALID_STATE;

            //Has to be added before esp_ble_gatts_app_register so we can access it in that callback.
            _serverProfiles.push_back(profile);

            esp_err_t retVal = esp_ble_gatts_app_register(profile->appId);
            if (retVal != ESP_OK)
                _serverProfiles.erase(FindServerProfile(profile->appId));

            _mutex.unlock();
            return retVal;
        }

        static esp_err_t UnregisterServerApp(uint16_t appId)
        {
            _mutex.lock();
            if (!_initialized)
            {
                _mutex.unlock();
                return ESP_ERR_INVALID_STATE;
            }

            auto iterator = FindServerProfile(appId);
            if (iterator == _serverProfiles.end())
            {
                _mutex.unlock();
                return ESP_ERR_INVALID_STATE;
            }

            esp_err_t retVal = ESP_OK;
            if ((*iterator)->gattsIf != ESP_GATT_IF_NONE)
                retVal = esp_ble_gatts_app_unregister((*iterator)->gattsIf);

            _serverProfiles.erase(iterator);

            _mutex.unlock();
            return retVal;
        }

        static esp_err_t RegisterClientApp(SGattClientProfile* profile)
        {
            _mutex.lock();
            if (!_initialized)
            {
                _mutex.unlock();
                return ESP_ERR_INVALID_STATE;
            }

            if (FindClientProfile(profile->appId) != _clientProfiles.end())
                return ESP_ERR_INVALID_STATE;

            //Has to be added before esp_ble_gatts_app_register so we can access it in that callback.
            _clientProfiles.push_back(profile);

            esp_err_t retVal = esp_ble_gattc_app_register(profile->appId);
            if (retVal != ESP_OK)
                _clientProfiles.erase(FindClientProfile(profile->appId));

            _mutex.unlock();
            return retVal;
        }

        static esp_err_t UnregisterClientApp(uint16_t appId)
        {
            _mutex.lock();
            if (!_initialized)
            {
                _mutex.unlock();
                return ESP_ERR_INVALID_STATE;
            }

            auto iterator = FindClientProfile(appId);
            if (iterator == _clientProfiles.end())
            {
                _mutex.unlock();
                return ESP_ERR_INVALID_STATE;
            }

            esp_err_t retVal = ESP_OK;
            if ((*iterator)->gattcIf != ESP_GATT_IF_NONE)
                retVal = esp_ble_gattc_app_unregister((*iterator)->gattcIf);

            _clientProfiles.erase(iterator);

            _mutex.unlock();
            return retVal;
        }
    };
};

bool ReadieFur::Network::Bluetooth::BLE::_initialized = false;
uint8_t ReadieFur::Network::Bluetooth::BLE::_manufacturer[16] = { 'E', 'S', 'P' };
uint8_t ReadieFur::Network::Bluetooth::BLE::_serviceUuid[16] =
{
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x18, 0x0D, 0x00, 0x00,
};
esp_ble_adv_data_t ReadieFur::Network::Bluetooth::BLE::_advertisingConfig =
{
    .set_scan_rsp = false,
    .include_txpower = true,
    .min_interval = 0x0006, //Slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval = 0x0010, //Slave connection max interval, Time = max_interval * 1.25 msec
    .appearance = 0x00,
    .manufacturer_len = sizeof(_manufacturer),
    .p_manufacturer_data = _manufacturer,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(_serviceUuid),
    .p_service_uuid = _serviceUuid,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};
esp_ble_adv_data_t ReadieFur::Network::Bluetooth::BLE::_rspConfig =
{
    .set_scan_rsp = true,
    .include_name = true,
    .manufacturer_len = sizeof(_manufacturer),
    .p_manufacturer_data = _manufacturer,
};
esp_ble_adv_params_t ReadieFur::Network::Bluetooth::BLE::_advertisingParams =
{
    .adv_int_min        = 0x100,
    .adv_int_max        = 0x100,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_RPA_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};
const char* ReadieFur::Network::Bluetooth::BLE::_deviceName = NULL;
uint32_t ReadieFur::Network::Bluetooth::BLE::_passkey;
std::mutex ReadieFur::Network::Bluetooth::BLE::_mutex;
uint8_t ReadieFur::Network::Bluetooth::BLE::_advConfigDone = 0;
std::vector<ReadieFur::Network::Bluetooth::SGattServerProfile*> ReadieFur::Network::Bluetooth::BLE::_serverProfiles;
std::vector<ReadieFur::Network::Bluetooth::SGattClientProfile*> ReadieFur::Network::Bluetooth::BLE::_clientProfiles;
