#pragma once

#include "Logging.hpp"
#include "Modem.hpp"
#include <esp_now.h>
#include <mutex>
#include <string.h>
#include <vector>
#include <algorithm>
#include <functional>
#include <stdint.h>
#include "Event/Event.hpp"

#define __ESP_NOW_HEADER UINT32_C(0xC679C7A5) //The header is used to recognise compatible messages received by the esp-now protocol, with the hopes that these bytes won't likely be found in other arbitrary data.
#define __ESP_NOW_VERSION UINT8_C(1) //The major version of the esp-now protocol, minor versions don't need to be checked as they should be compatible with the same major version.
#define __ESP_NOW_LOCK() std::lock_guard<std::mutex> lock(_mutex); if (!_initalized) return ESP_ERR_INVALID_STATE;
#define __ESP_NOW_IF WIFI_IF_STA

namespace ReadieFur::Network::WiFi
{
    class EspNow
    {
    public:
        typedef std::function<void(const esp_now_recv_info_t* info, const uint8_t* data, int len)> TEspNowReceiveCallback;
        static Event::Event<const uint8_t*, uint8_t, bool> OnPeerDiscovered;

    private:
        static const uint8_t BROADCAST_ADDRESS[ESP_NOW_ETH_ALEN];
        static std::mutex _mutex;
        static bool _initalized;
        static bool _doEncryption;
        static std::vector<uint8_t*> _peers;
        static std::vector<TEspNowReceiveCallback> _receiveCallbacks;

        enum EOperation : uint8_t
        {
            EspNowOp_Invalid,
            EspNowOp_Message = 1, //Must always remain as 1 for cross version compatibility (though all devices should use the same major version).
            EspNowOp_QueryPeers,
            EspNowOp_QueryPeersResponse,
            EspNowOp_Max
        };

        static void OnReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len)
        {
            //Check if the data is for a QueryPeers request/response.
            EOperation operation;
            const uint8_t* payload;
            int payloadLen;
            esp_err_t readError = Read(data, len, operation, payload, payloadLen);
            if (readError != ESP_OK)
            {
                LOGV(nameof(WiFi::EspNow), "Received invalid message: %s", esp_err_to_name(readError));
                return;
            }

            switch (operation)
            {
                case EOperation::EspNowOp_Message:
                {
                    //Forward the message onto any registered callbacks.
                    for (auto &&callback : _receiveCallbacks)
                        callback(info, payload, payloadLen);
                    break;
                }
                case EOperation::EspNowOp_QueryPeers:
                {
                    //Respond with own info.
                    uint8_t* selfMac = new uint8_t[ESP_NOW_ETH_ALEN];
                    esp_wifi_get_mac(__ESP_NOW_IF, selfMac);

                    //Get channel.
                    uint8_t channel;
                    wifi_second_chan_t second;
                    esp_wifi_get_channel(&channel, &second);

                    bool doEncryption = _doEncryption;

                    //Build response.
                    //Mac address not included in the payload as it will already be included in the protocol.
                    uint8_t responsePayload[sizeof(uint8_t) + sizeof(bool)];
                    // memcpy(responsePayload, selfMac, ESP_NOW_ETH_ALEN);
                    *(responsePayload) = channel;
                    *(responsePayload + sizeof(uint8_t)) = doEncryption;

                    Send(EOperation::EspNowOp_QueryPeersResponse, selfMac, responsePayload, sizeof(responsePayload));
                    break;
                }
                case EOperation::EspNowOp_QueryPeersResponse:
                {
                    //Build peer info.
                    uint8_t* peerMac = info->src_addr;
                    uint8_t channel = *(payload);
                    bool doEncryption = *(payload + sizeof(uint8_t));
                    OnPeerDiscovered.Dispatch(peerMac, channel, doEncryption);
                    break;
                }
                case EOperation::EspNowOp_Invalid:
                default:
                {
                    //Shouldn't happen as the read function should catch this.
                    LOGV(nameof(WiFi::EspNow), "Received unknown operation: %i", operation);
                    return;
                }
            }
        }

        static esp_err_t Send(EOperation operation, const uint8_t* peerMac, const uint8_t* payload, int payloadLen)
        {
            uint8_t* buffer = new uint8_t[sizeof(uint32_t) + sizeof(uint8_t) + sizeof(EOperation) + payloadLen];
            if (buffer == nullptr)
                return ESP_ERR_NO_MEM;

            *((uint32_t*)buffer) = __ESP_NOW_HEADER; //Works without memcpy because the type is of a known size is being written.
            *(buffer + sizeof(uint32_t)) = __ESP_NOW_VERSION;
            *(EOperation*)(buffer + sizeof(uint32_t) + sizeof(uint8_t)) = operation;
            memcpy(buffer + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(EOperation), payload, payloadLen);

            esp_err_t err = esp_now_send(peerMac, buffer, sizeof(uint32_t) + sizeof(uint8_t) + sizeof(EOperation) + payloadLen);
            delete[] buffer;

            return err;
        }

        static esp_err_t Read(const uint8_t* message, int messageLen, EOperation& operation, const uint8_t*& payload, int& payloadLen)
        {
            if (messageLen < sizeof(uint32_t) + sizeof(EOperation))
                return ESP_ERR_INVALID_SIZE;

            if (*((uint32_t*)message) != __ESP_NOW_HEADER)
                return ESP_ERR_INVALID_RESPONSE;

            if (*(message + sizeof(uint32_t)) != __ESP_NOW_VERSION)
                return ESP_ERR_INVALID_VERSION;

            operation = *(EOperation*)(message + sizeof(uint32_t) + sizeof(uint8_t));
            if (operation == EOperation::EspNowOp_Invalid || operation >= EOperation::EspNowOp_Max)
                return ESP_ERR_INVALID_RESPONSE;

            payload = message + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(EOperation);
            payloadLen = messageLen - (sizeof(uint32_t) + sizeof(uint8_t) + sizeof(EOperation));

            return ESP_OK;
        }

    public:
        static esp_err_t Init(const uint8_t* encryptionKey = nullptr)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_initalized)
                return ESP_OK;

            esp_err_t err;

            if (!Modem::Initalized())
            {
                LOGE(nameof(WiFi::EspNow), "WiFi not initialized.");
                return ESP_ERR_INVALID_STATE;
            }

            //For this module all messages will be sent in STA mode (mesh network).
            if (Modem::IsInterfaceEnabled(__ESP_NOW_IF) && (err = Modem::EnableInterface(__ESP_NOW_IF) != ESP_OK))
            {
                LOGE(nameof(WiFi::EspNow), "Failed to enable STA interface: %s", esp_err_to_name(err));
                return err;
            }

            err = esp_now_init();
            if (err != ESP_OK)
            {
                LOGE(nameof(WiFi::EspNow), "Failed to initialize ESP-NOW: %s", esp_err_to_name(err));
                return err;
            }

            if (encryptionKey != nullptr)
            {
                err = esp_now_set_pmk(encryptionKey);
                if (err != ESP_OK)
                {
                    LOGE(nameof(WiFi::EspNow), "Failed to set encryption key: %s", esp_err_to_name(err));
                    return err;
                }
                _doEncryption = true;
            }
            else
            {
                _doEncryption = false;
            }

            //Documentation requires the broadcast peer to be added in order for broadcast functionality to work.
            esp_now_peer_info_t peerInfo;
            memcpy(peerInfo.peer_addr, BROADCAST_ADDRESS, ESP_NOW_ETH_ALEN);
            peerInfo.encrypt = false;
            peerInfo.channel = 0;
            peerInfo.ifidx = __ESP_NOW_IF; //Assume STA for all peers as that is what this module will be targeting.
            err = esp_now_add_peer(&peerInfo);
            if (err != ESP_OK)
            {
                LOGE(nameof(WiFi::EspNow), "Failed to add broadcast peer: %s", esp_err_to_name(err));
                return err;
            }
            uint8_t* peerMacCopy = new uint8_t[ESP_NOW_ETH_ALEN];
            memcpy(peerMacCopy, BROADCAST_ADDRESS, ESP_NOW_ETH_ALEN);
            _peers.push_back(peerMacCopy);

            err = esp_now_register_recv_cb(OnReceive);
            if (err != ESP_OK)
            {
                LOGE(nameof(WiFi::EspNow), "Failed to register receive callback: %s", esp_err_to_name(err));
                return err;
            }

            _initalized = true;
            return ESP_OK;
        }

        static esp_err_t Deinit()
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (!_initalized)
                return ESP_OK;

            esp_err_t err = esp_now_deinit();
            if (err != ESP_OK)
            {
                LOGE(nameof(WiFi::EspNow), "Failed to deinitialize ESP-NOW: %s", esp_err_to_name(err));
                return err;
            }

            for (auto &&peer : _peers)
                delete[] peer;
            _peers.clear();

            _initalized = false;
            return ESP_OK;
        }

        static inline bool Initalized()
        {
            return _initalized;
        }

        static esp_err_t AddOrUpdatePeer(const uint8_t* peerMac, const uint8_t* peerKey = 0)
        {
            __ESP_NOW_LOCK();

            esp_now_peer_info_t peerInfo;
            memcpy(peerInfo.peer_addr, peerMac, ESP_NOW_ETH_ALEN);
            if (peerKey != 0)
            {
                peerInfo.encrypt = true;
                memcpy(peerInfo.lmk, peerKey, ESP_NOW_KEY_LEN);
            }
            else
            {
                peerInfo.encrypt = false;
            }
            peerInfo.channel = 0;
            peerInfo.ifidx = __ESP_NOW_IF;

            esp_err_t err = esp_now_add_peer(&peerInfo);
            if (err == ESP_ERR_ESPNOW_EXIST)
            {
                // LOGW(nameof(WiFi::EspNow), "Peer already exists.");
                err = esp_now_mod_peer(&peerInfo);
            }
            
            if (err != ESP_OK)
            {
                LOGE(nameof(WiFi::EspNow), "Failed to add peer: %s", esp_err_to_name(err));
                return err;
            }

            uint8_t* peerMacCopy = new uint8_t[ESP_NOW_ETH_ALEN];
            memcpy(peerMacCopy, peerMac, ESP_NOW_ETH_ALEN);
            _peers.push_back(peerMacCopy);

            return ESP_OK;
        }

        static esp_err_t RemovePeer(const uint8_t* peerMac)
        {
            __ESP_NOW_LOCK();

            esp_err_t err = esp_now_del_peer(peerMac);
            if (err != ESP_OK)
            {
                LOGE(nameof(WiFi::EspNow), "Failed to remove peer: %s", esp_err_to_name(err));
                return err;
            }

            return ESP_OK;
        }

        static esp_err_t SetPowerSaving(uint16_t wakeIntervalMs = ESP_WIFI_CONNECTIONLESS_INTERVAL_DEFAULT_MODE)
        {
            __ESP_NOW_LOCK();

            esp_err_t err = esp_now_set_wake_window(wakeIntervalMs);
            if (err != ESP_OK)
            {
                LOGE(nameof(WiFi::EspNow), "Failed to set power saving: %s", esp_err_to_name(err));
                return err;
            }

            err = esp_wifi_connectionless_module_set_wake_interval(wakeIntervalMs);
            if (err != ESP_OK)
            {
                LOGE(nameof(WiFi::EspNow), "Failed to set power saving: %s", esp_err_to_name(err));
                return err;
            }

            return ESP_OK;
        }

        static esp_err_t RegisterOnReceiveCallback(TEspNowReceiveCallback callback)
        {
            __ESP_NOW_LOCK();
            _receiveCallbacks.push_back(callback);
            return ESP_OK;
        }

        static esp_err_t UnregisterOnReceiveCallback(TEspNowReceiveCallback callback)
        {
            __ESP_NOW_LOCK();
            //Erase based on underlying pointer.
            _receiveCallbacks.erase(std::remove_if(_receiveCallbacks.begin(), _receiveCallbacks.end(), [&callback](TEspNowReceiveCallback& item) { return item.target<void>() == callback.target<void>(); }), _receiveCallbacks.end());
            return ESP_OK;
        }

        static esp_err_t MessagePeer(const uint8_t* peerMac, const uint8_t* data, size_t len)
        {
            __ESP_NOW_LOCK();

            esp_err_t err = Send(EOperation::EspNowOp_Message, peerMac, data, len);
            if (err != ESP_OK)
            {
                LOGE(nameof(WiFi::EspNow), "Failed to send data: %s", esp_err_to_name(err));
                return err;
            }

            return ESP_OK;
        }

        static esp_err_t BroadcastRegistered(const uint8_t* data, size_t len)
        {
            __ESP_NOW_LOCK();

            //Because we add the broadcast peer on initialization, we can't use the NULL parameter to send to to registered peers otherwise it will also send to the broadcast address.
            // esp_err_t err = esp_now_send(NULL, data, len);

            bool hadErrors = false;
            for (auto &&peer : _peers)
            {
                if (memcmp(peer, BROADCAST_ADDRESS, ESP_NOW_ETH_ALEN) == 0)
                    continue;

                esp_err_t err = Send(EOperation::EspNowOp_Message, peer, data, len);
                if (err != ESP_OK)
                {
                    LOGE(nameof(WiFi::EspNow), "Failed to broadcast data to registered client '%02X:%02X:%02X:%02X:%02X:%02X': %s",
                        peer[0], peer[1], peer[2], peer[3], peer[4], peer[5], esp_err_to_name(err));
                    hadErrors = true;
                }
            }

            return hadErrors ? ESP_FAIL : ESP_OK;
        }

        static esp_err_t BroadcastAll(const uint8_t* data, size_t len)
        {
            __ESP_NOW_LOCK();

            esp_err_t err = Send(EOperation::EspNowOp_Message, BROADCAST_ADDRESS, data, len);
            if (err != ESP_OK)
            {
                LOGE(nameof(WiFi::EspNow), "Failed to broadcast data: %s", esp_err_to_name(err));
                return err;
            }

            return ESP_OK;
        }

        static esp_err_t ScanForPeers()
        {
            __ESP_NOW_LOCK();

            esp_err_t err = Send(EOperation::EspNowOp_QueryPeers, BROADCAST_ADDRESS, nullptr, 0);
            if (err != ESP_OK)
            {
                LOGE(nameof(WiFi::EspNow), "Failed to query peers: %s", esp_err_to_name(err));
                return err;
            }

            return ESP_OK;
        }
    };
};

const uint8_t ReadieFur::Network::WiFi::EspNow::BROADCAST_ADDRESS[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
std::mutex ReadieFur::Network::WiFi::EspNow::_mutex;
bool ReadieFur::Network::WiFi::EspNow::_initalized = false;
bool ReadieFur::Network::WiFi::EspNow::_doEncryption = false;
std::vector<uint8_t*> ReadieFur::Network::WiFi::EspNow::_peers;
std::vector<ReadieFur::Network::WiFi::EspNow::TEspNowReceiveCallback> ReadieFur::Network::WiFi::EspNow::_receiveCallbacks;
ReadieFur::Event::Event<const uint8_t*, uint8_t, bool> ReadieFur::Network::WiFi::EspNow::OnPeerDiscovered;
