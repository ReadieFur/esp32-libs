#pragma once

#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <esp_log.h>
#include <esp_system.h>
#include "Logging.hpp"
#include <freertos/semphr.h>
#include "Network/WiFi.hpp"

namespace ReadieFur::Network::OTA
{
    class API
    {
    private:
        static SemaphoreHandle_t _instanceMutex;
        static httpd_config_t _config;
        static httpd_handle_t _server;
        static esp_ota_handle_t _otaHandle;
        static const esp_partition_t* _otaPartition;

        static esp_err_t OtaStart(httpd_req_t* req)
        {
            LOGI(nameof(OTA::API), "OTA upload started.");

            _otaPartition = esp_ota_get_next_update_partition(NULL);
            if (_otaPartition == nullptr)
            {
                LOGE(nameof(OTA::API), "No OTA partition found.");
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition found.");
                return ESP_FAIL;
            }

            esp_err_t err = esp_ota_begin(_otaPartition, OTA_SIZE_UNKNOWN, &_otaHandle);
            if (err != ESP_OK)
            {
                LOGE(nameof(OTA::API), "esp_ota_begin failed: %s", esp_err_to_name(err));
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
                return ESP_FAIL;
            }

            LOGV(nameof(OTA::API), "OTA initialized.");
            httpd_resp_set_status(req, "200 OK");
            httpd_resp_send(req, "OTA Initialized", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }

        static esp_err_t OtaData(httpd_req_t* req)
        {
            char buf[1024];
            int received;

            while ((received = httpd_req_recv(req, buf, sizeof(buf))) > 0)
            {
                esp_err_t err = esp_ota_write(_otaHandle, buf, received);
                if (err != ESP_OK)
                {
                    LOGE(nameof(OTA::API), "OTA write failed: %s", esp_err_to_name(err));
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
                    return ESP_FAIL;
                }
            }

            if (received < 0)
            {
                if (received == HTTPD_SOCK_ERR_TIMEOUT)
                {
                    httpd_resp_send_408(req);
                }
                LOGE(nameof(OTA::API), "File reception failed.");
                return ESP_FAIL;
            }

            LOGI(nameof(OTA::API), "OTA data written.");
            httpd_resp_set_status(req, "200 OK");
            httpd_resp_send(req, "Data received", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }

        static esp_err_t OtaComplete(httpd_req_t* req)
        {
            esp_err_t err = esp_ota_end(_otaHandle);
            _otaHandle = 0;
            if (err != ESP_OK)
            {
                LOGE(nameof(OTA::API), "OTA end failed: %s", esp_err_to_name(err));
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
                return ESP_FAIL;
            }

            err = esp_ota_set_boot_partition(_otaPartition);
            if (err != ESP_OK)
            {
                LOGE(nameof(API::OTA), "Failed to set boot partition: %s", esp_err_to_name(err));
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set boot partition");
                return ESP_FAIL;
            }

            LOGI(nameof(OTA::API), "OTA complete, restarting...");
            httpd_resp_set_status(req, "202 Accepted");
            httpd_resp_send(req, "OTA Complete, Restarting...", HTTPD_RESP_USE_STRLEN);
            esp_restart();

            return ESP_OK;
        }

    public:
        static esp_err_t Init()
        {
            if (xSemaphoreTake(_instanceMutex, 0) != pdTRUE)
            {
                LOGE(nameof(OTA::API), "Failed to lock instance.");
                return ESP_FAIL;
            }

            wifi_mode_t wifiMode = WiFi::GetMode();
            if (wifiMode != WIFI_MODE_STA && wifiMode != WIFI_MODE_AP && wifiMode != WIFI_MODE_APSTA)
            {
                xSemaphoreGive(_instanceMutex);
                LOGE(nameof(OTA::API), "WiFi not in AP or STA mode.");
                return ESP_ERR_INVALID_STATE;
            }

            esp_err_t err = ESP_OK;
            if ((err = httpd_start(&_server, &_config)) != ESP_OK)
            {
                xSemaphoreGive(_instanceMutex);
                LOGE(nameof(OTA::API), "Failed to start HTTP server.");
                return err;
            }

            httpd_uri_t _uriStart = {
                .uri = "/ota/start",
                .method = HTTP_POST,
                .handler = OtaStart,
                .user_ctx = NULL
            };
            httpd_uri_t _uriData = {
                .uri = "/ota/data",
                .method = HTTP_POST,
                .handler = OtaData,
                .user_ctx = NULL
            };
            httpd_uri_t _uriComplete = {
                .uri = "/ota/complete",
                .method = HTTP_POST,
                .handler = OtaComplete,
                .user_ctx = NULL
            };
            if ((err = httpd_register_uri_handler(_server, &_uriStart)) != ESP_OK
                || (err = httpd_register_uri_handler(_server, &_uriData)) != ESP_OK
                || (err = httpd_register_uri_handler(_server, &_uriComplete)) != ESP_OK)
            {
                xSemaphoreGive(_instanceMutex);
                LOGE(nameof(OTA::API), "Failed to register URI handler.");
                return err;
            }

            LOGV(nameof(OTA::API), "HTTP server started.");
            return ESP_OK;
        }

        static void Deinit()
        {
            httpd_stop(_server);
            _server = NULL;
            _otaHandle = 0;
            _otaPartition = nullptr;
            xSemaphoreGive(_instanceMutex);
            LOGV(nameof(OTA::API), "HTTP server stopped.");
        }
    };
};

SemaphoreHandle_t ReadieFur::Network::OTA::API::_instanceMutex = xSemaphoreCreateMutex();
httpd_config_t ReadieFur::Network::OTA::API::_config = HTTPD_DEFAULT_CONFIG();
httpd_handle_t ReadieFur::Network::OTA::API::_server = NULL;
esp_ota_handle_t ReadieFur::Network::OTA::API::_otaHandle = 0;
const esp_partition_t* ReadieFur::Network::OTA::API::_otaPartition = nullptr;
