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
        static httpd_handle_t _server;
        static esp_ota_handle_t _otaHandle;
        static const esp_partition_t* _otaPartition;

        static esp_err_t OtaProcess(httpd_req_t* req)
        {
            esp_err_t err;

            //Ensure we're not starting another OTA process during an ongoing one.
            if (_otaHandle != 0)
            {
                LOGE(nameof(OTA::API), "An OTA process is already ongoing.");
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "An OTA process is already in progress");
                return ESP_FAIL;
            }

            //Get the next available OTA partition.
            _otaPartition = esp_ota_get_next_update_partition(NULL);
            if (_otaPartition == nullptr)
            {
                LOGE(nameof(OTA::API), "No OTA partition found.");
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition found.");
                return ESP_FAIL;
            }

            //Start the OTA process.
            LOGI(nameof(OTA::API), "OTA update started...");
            err = esp_ota_begin(_otaPartition, OTA_SIZE_UNKNOWN, &_otaHandle);
            if (err != ESP_OK)
            {
                LOGE(nameof(OTA::API), "esp_ota_begin failed: %s", esp_err_to_name(err));
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
                esp_ota_abort(_otaHandle);
                _otaHandle = 0;
                return ESP_FAIL;
            }
            LOGV(nameof(OTA::API), "OTA partition initialized.");

            //Receive the data and write it to OTA.
            TickType_t lastLog = 0;
            char buf[1024];
            int received;
            int totalReceived = 0;
            while ((received = httpd_req_recv(req, buf, sizeof(buf))) > 0)
            {
                totalReceived += received;
                if (xTaskGetTickCount() - lastLog > pdMS_TO_TICKS(500))
                {
                    LOGV(nameof(OTA::API), "Received %d/%d bytes...", totalReceived, req->content_len);
                    lastLog = xTaskGetTickCount();
                }

                err = esp_ota_write(_otaHandle, buf, received);
                if (err != ESP_OK)
                {
                    LOGE(nameof(OTA::API), "OTA write failed: %s", esp_err_to_name(err));
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
                    esp_ota_abort(_otaHandle);
                    _otaHandle = 0;
                    return ESP_FAIL;
                }
            }

            if (received < 0)
            {
                if (received == HTTPD_SOCK_ERR_TIMEOUT)
                    httpd_resp_send_408(req);
                LOGE(nameof(OTA::API), "OTA file receive failed.");
                return ESP_FAIL;
            }
            LOGI(nameof(OTA::API), "OTA file received.");

            //Finalize OTA and set boot partition.
            err = esp_ota_end(_otaHandle);
            _otaHandle = 0;
            if (err != ESP_OK)
            {
                LOGE(nameof(OTA::API), "OTA end failed: %s", esp_err_to_name(err));
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
                return ESP_FAIL;
            }

            //https://www.esp32.com/viewtopic.php?t=2998
            err = esp_ota_set_boot_partition(_otaPartition);
            if (err != ESP_OK)
            {
                LOGE(nameof(OTA::API), "Failed to set boot partition: %s", esp_err_to_name(err));
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set boot partition");
                return ESP_FAIL;
            }

            LOGI(nameof(OTA::API), "OTA complete, restarting...");
            httpd_resp_set_status(req, "202 Accepted");
            httpd_resp_send(req, "OTA Complete, Restarting...", HTTPD_RESP_USE_STRLEN);
            //Give the response time to send, from what I can tell the above function should be blocking but I think the
            //restart call happens before it can properly send as the client doesn't get the response without this added delay.
            vTaskDelay(pdMS_TO_TICKS(50));
            esp_restart();

            return ESP_OK;
        }

    public:
        static esp_err_t Init(httpd_config_t* config = nullptr)
        {
            if (config == nullptr)
                return ESP_ERR_INVALID_ARG;

            if (xSemaphoreTake(_instanceMutex, 0) != pdTRUE)
            {
                LOGE(nameof(OTA::API), "Failed to lock instance.");
                return ESP_FAIL;
            }

            if (!WiFi::Initalized())
            {
                xSemaphoreGive(_instanceMutex);
                LOGE(nameof(OTA::API), "WiFi not initialized.");
                return ESP_ERR_INVALID_STATE;
            }

            esp_err_t err = ESP_OK;
            if ((err = httpd_start(&_server, config)) != ESP_OK)
            {
                xSemaphoreGive(_instanceMutex);
                LOGE(nameof(OTA::API), "Failed to start HTTP server.");
                return err;
            }

            httpd_uri_t _uriProcess = {
                .uri = "/ota",
                .method = HTTP_POST,
                .handler = OtaProcess,
                .user_ctx = NULL
            };
            if ((err = httpd_register_uri_handler(_server, &_uriProcess)) != ESP_OK)
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
httpd_handle_t ReadieFur::Network::OTA::API::_server = NULL;
esp_ota_handle_t ReadieFur::Network::OTA::API::_otaHandle = 0;
const esp_partition_t* ReadieFur::Network::OTA::API::_otaPartition = nullptr;
