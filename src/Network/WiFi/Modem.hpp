#pragma once

#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_wifi.h>
#include "Event/Event.hpp"
#include <mutex>

#define __MODEM_LOCK() std::lock_guard<std::mutex> lock(_mutex); if (!_initalized) return ESP_ERR_INVALID_STATE;

namespace ReadieFur::Network::WiFi
{
    class Modem
    {
    private:
        static std::mutex _mutex;
        static bool _initalized;
        static esp_netif_t* _staNet;
        static esp_netif_t* _apNet;

        static esp_err_t EnableInterfaceInternal(wifi_interface_t interface)
        {
            esp_err_t err;

            if (interface == WIFI_IF_STA && _staNet == nullptr)
                _staNet = esp_netif_create_default_wifi_sta();
            else if (interface == WIFI_IF_AP && _apNet == nullptr)
                _apNet = esp_netif_create_default_wifi_ap();

            //Combine the new mode with the existing mode.
            wifi_mode_t mode;
            esp_wifi_get_mode(&mode);
            if ((interface == WIFI_IF_STA && (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA))
                || (interface == WIFI_IF_AP && (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA)))
                mode = WIFI_MODE_APSTA;
            else if (interface == WIFI_IF_STA)
                mode = WIFI_MODE_STA;
            else if (interface == WIFI_IF_AP)
                mode = WIFI_MODE_AP;
            err = esp_wifi_set_mode(mode);

            //OnModeChanged is not called here as it isn't fully changed until esp_wifi_start is called.

            return err;
        }

    public:
        static Event::Event<wifi_mode_t> OnModeChanged;

        static esp_err_t Init()
        {
            std::lock_guard<std::mutex> lock(_mutex);

            //Check if the instance is already initialized.
            if (Initalized())
                return ESP_OK;

            esp_err_t err;
            // esp_err_t err = nvs_flash_init();
            // if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
            // {
            //     //NVS partition was modified and needs to be erased.
            //     nvs_flash_erase();
            //     err = nvs_flash_init();
            // }
            // if (err != ESP_OK)
            // {
            //     xSemaphoreGive(_instanceMutex);
            //     return err;
            // }

            if ((err = esp_netif_init()) != ESP_OK)
                return err;

            err = esp_event_loop_create_default();
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
                return err;

            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            if ((err = esp_wifi_init(&cfg)) != ESP_OK)
            {
                // esp_event_loop_delete_default();
                return err;
            }

            _initalized = true;
            return ESP_OK;
        }

        static esp_err_t Deinit()
        {
            std::lock_guard<std::mutex> lock(_mutex);

            esp_err_t err = ESP_OK;
            if ((err = esp_wifi_stop()) != ESP_OK)
                return err;

            esp_netif_destroy_default_wifi(_staNet);
            _staNet = nullptr;
            esp_netif_destroy_default_wifi(_apNet);
            _apNet = nullptr;

            // if ((err = esp_event_loop_delete_default()) != ESP_OK)
            //     return err;

            if ((err = esp_wifi_deinit()) != ESP_OK)
                return err;

            _initalized = false;
            return err;
        }

        static inline bool Initalized()
        {
            return _initalized;
        }

        static esp_err_t ConfigureInterface(wifi_interface_t interface, wifi_config_t config)
        {
            __MODEM_LOCK();

            esp_err_t err;

            if ((err = esp_wifi_stop()) != ESP_OK)
            {
                LOGE(nameof(Modem), "Failed to reconfigure WiFi: %s", esp_err_to_name(err));
                return err;
            }

            //An interface can only be configured if it is enabled, as per the documentation.
            if ((err = EnableInterfaceInternal(interface)) != ESP_OK)
            {
                LOGE(nameof(Modem), "Failed to enable interface (%i): %s", interface, esp_err_to_name(err));
                return err;
            }

            if ((err = esp_wifi_set_config(interface, &config)) != ESP_OK)
            {
                LOGE(nameof(Modem), "Failed to configure WiFi interface (%i): %s", interface, esp_err_to_name(err));
                return err;
            }

            if ((err = esp_wifi_start()) != ESP_OK)
            {
                LOGE(nameof(Modem), "Failed to start WiFi interface (%i): %s", interface, esp_err_to_name(err));
                return err;
            }

            wifi_mode_t mode;
            esp_wifi_get_mode(&mode);
            OnModeChanged.Dispatch(mode);

            return ESP_OK;
        }

        static esp_err_t ShutdownInterface(wifi_interface_t interface)
        {
            __MODEM_LOCK();

            esp_err_t err;

            //Remove the interface from the mode.
            wifi_mode_t mode;
            esp_wifi_get_mode(&mode);
            if (interface == WIFI_IF_STA && mode == WIFI_MODE_APSTA)
                mode = WIFI_MODE_AP;
            else if (interface == WIFI_IF_AP && mode == WIFI_MODE_APSTA)
                mode = WIFI_MODE_STA;
            else
                mode = WIFI_MODE_NULL;
            err = esp_wifi_set_mode(mode);

            if (interface == WIFI_IF_STA)
            {
                esp_netif_destroy_default_wifi(_staNet);
                // esp_netif_destroy(_staNet);
                _staNet = nullptr;
            }
            else if (interface == WIFI_IF_AP)
            {
                esp_netif_destroy_default_wifi(_apNet);
                // esp_netif_destroy(_apNet);
                _apNet = nullptr;
            }

            OnModeChanged.Dispatch(mode);

            return err;
        }

        static esp_err_t EnableInterface(wifi_interface_t interface)
        {
            __MODEM_LOCK();

            esp_err_t err;

            if ((err = esp_wifi_stop()) != ESP_OK)
            {
                LOGE(nameof(Modem), "Failed to reconfigure WiFi: %s", esp_err_to_name(err));
                return err;
            }

            if ((err = EnableInterfaceInternal(interface)) != ESP_OK)
            {
                LOGE(nameof(Modem), "Failed to enable interface (%i): %s", interface, esp_err_to_name(err));
                return err;
            }

            if ((err = esp_wifi_start()) != ESP_OK)
            {
                LOGE(nameof(Modem), "Failed to start WiFi: %s", esp_err_to_name(err));
                return err;
            }

            wifi_mode_t mode;
            esp_wifi_get_mode(&mode);
            OnModeChanged.Dispatch(mode);

            return ESP_OK;
        }

        static esp_err_t GetInterfaceConfig(wifi_interface_t interface, wifi_config_t *config)
        {
            __MODEM_LOCK();
            return esp_wifi_get_config(interface, config);
        }

        static bool IsInterfaceConfigured(wifi_interface_t interface)
        {
            __MODEM_LOCK();

            wifi_config_t config;
            esp_err_t err = esp_wifi_get_config(interface, &config);
            return err == ESP_OK;
        }

        //An interface can be enabled but not configured, this can happen in cases like esp-now or between calls made in the interface setup and configuration.
        static bool IsInterfaceEnabled(wifi_interface_t interface)
        {
            __MODEM_LOCK();
            wifi_mode_t mode;
            esp_wifi_get_mode(&mode);
            return (interface == WIFI_IF_STA && (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA))
                || (interface == WIFI_IF_AP && (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA));
        }

        static wifi_mode_t GetMode()
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (!Initalized())
                return WIFI_MODE_MAX;

            wifi_mode_t mode;
            esp_wifi_get_mode(&mode);
            return mode;
        }
    };
};

std::mutex ReadieFur::Network::WiFi::Modem::_mutex;
bool ReadieFur::Network::WiFi::Modem::_initalized = false;
esp_netif_t* ReadieFur::Network::WiFi::Modem::_staNet = nullptr;
esp_netif_t* ReadieFur::Network::WiFi::Modem::_apNet = nullptr;
ReadieFur::Event::Event<wifi_mode_t> ReadieFur::Network::WiFi::Modem::OnModeChanged = Event::Event<wifi_mode_t>();
