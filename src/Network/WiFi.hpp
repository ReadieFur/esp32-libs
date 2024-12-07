#pragma once

#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_wifi.h>
#include "Event/Event.hpp"

namespace ReadieFur::Network
{
    class WiFi
    {
    private:
        static SemaphoreHandle_t _instanceMutex;
        static bool _initalized;
        static esp_netif_t* _staNet;
        static esp_netif_t* _apNet;

    public:
        static Event::Event<wifi_mode_t> OnModeChanged;

        static esp_err_t Init()
        {
            if (xSemaphoreTake(_instanceMutex, portMAX_DELAY) != pdTRUE)
                return ESP_FAIL;

            //Check if the instance is already initialized.
            if (Initalized())
            {
                xSemaphoreGive(_instanceMutex);
                return ESP_OK;
            }

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
            {
                xSemaphoreGive(_instanceMutex);
                return err;
            }

            err = esp_event_loop_create_default();
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
            {
                xSemaphoreGive(_instanceMutex);
                return err;
            }

            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            if ((err = esp_wifi_init(&cfg)) != ESP_OK)
            {
                // esp_event_loop_delete_default();
                xSemaphoreGive(_instanceMutex);
                return err;
            }

            _initalized = true;
            xSemaphoreGive(_instanceMutex);
            return ESP_OK;
        }

        static esp_err_t Deinit()
        {
            if (xSemaphoreTake(_instanceMutex, portMAX_DELAY) != pdTRUE)
                return ESP_FAIL;

            esp_err_t err = ESP_OK;
            if ((err = esp_wifi_stop()) != ESP_OK)
            {
                xSemaphoreGive(_instanceMutex);
                return err;
            }

            esp_netif_destroy_default_wifi(_staNet);
            _staNet = nullptr;
            esp_netif_destroy_default_wifi(_apNet);
            _apNet = nullptr;

            // if ((err = esp_event_loop_delete_default()) != ESP_OK)
            // {
            //     xSemaphoreGive(_instanceMutex);
            //     return err;
            // }

            if ((err = esp_wifi_deinit()) != ESP_OK)
            {
                xSemaphoreGive(_instanceMutex);
                return err;
            }

            xSemaphoreGive(_instanceMutex);
            return err;
        }

        static bool Initalized()
        {
            return _initalized;
        }

        static esp_err_t ConfigureInterface(wifi_interface_t interface, wifi_config_t config)
        {
            if (xSemaphoreTake(_instanceMutex, portMAX_DELAY) != pdTRUE) { return ESP_FAIL; }
            if (!Initalized()) { xSemaphoreGive(_instanceMutex); return ESP_ERR_INVALID_STATE; }

            esp_wifi_stop();

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
            esp_wifi_set_mode(mode);

            esp_err_t err;
            if ((err = esp_wifi_set_config(interface, &config)) != ESP_OK
                || (err = esp_wifi_start()) != ESP_OK)
            {
                LOGE(nameof(WiFi), "Failed to configure WiFi interface (%i): %s", interface, esp_err_to_name(err));
                xSemaphoreGive(_instanceMutex);
                return err;
            }

            xSemaphoreGive(_instanceMutex);
            OnModeChanged.Dispatch(mode);
            return err;
        }

        static esp_err_t ShutdownInterface(wifi_interface_t interface)
        {
            if (xSemaphoreTake(_instanceMutex, portMAX_DELAY) != pdTRUE) { return ESP_FAIL; }
            if (!Initalized()) { xSemaphoreGive(_instanceMutex); return ESP_ERR_INVALID_STATE; }

            esp_err_t err = ESP_OK;

            //Remove the interface from the mode.
            wifi_mode_t mode;
            esp_wifi_get_mode(&mode);
            if (interface == WIFI_IF_STA && mode == WIFI_MODE_APSTA)
                mode = WIFI_MODE_AP;
            else if (interface == WIFI_IF_AP && mode == WIFI_MODE_APSTA)
                mode = WIFI_MODE_STA;
            else
                mode = WIFI_MODE_NULL;
            esp_wifi_set_mode(mode);

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

            xSemaphoreGive(_instanceMutex);
            return err;
        }

        static esp_err_t GetInterfaceConfig(wifi_interface_t interface, wifi_config_t *config)
        {
            if (xSemaphoreTake(_instanceMutex, portMAX_DELAY) != pdTRUE) { return ESP_FAIL; }
            if (!Initalized()) { xSemaphoreGive(_instanceMutex); return ESP_ERR_INVALID_STATE; }

            esp_err_t err = esp_wifi_get_config(interface, config);

            xSemaphoreGive(_instanceMutex);
            return err;
        }

        static wifi_mode_t GetMode()
        {
            if (xSemaphoreTake(_instanceMutex, portMAX_DELAY) != pdTRUE) { return WIFI_MODE_MAX; }
            if (!Initalized()) { xSemaphoreGive(_instanceMutex); return WIFI_MODE_MAX; }

            wifi_mode_t mode;
            esp_wifi_get_mode(&mode);
            return mode;

            xSemaphoreGive(_instanceMutex);
        }
    };
};

SemaphoreHandle_t ReadieFur::Network::WiFi::_instanceMutex = xSemaphoreCreateMutex();
bool ReadieFur::Network::WiFi::_initalized = false;
esp_netif_t* ReadieFur::Network::WiFi::_staNet = nullptr;
esp_netif_t* ReadieFur::Network::WiFi::_apNet = nullptr;
ReadieFur::Event::Event<wifi_mode_t> ReadieFur::Network::WiFi::OnModeChanged = Event::Event<wifi_mode_t>();
