#pragma once

// #include "Service/AService.hpp"
#include <esp_log.h>
#include <driver/uart.h>
#if defined(WebSerial)
#include <WebSerial.h>
#elif defined(WebSerialLite)
#include <WebSerialLite.h>
#endif

#define WRITE(format, ...) ReadieFur::Logging::Write(format, ##__VA_ARGS__)

#if CONFIG_LOG_TIMESTAMP_SOURCE_RTOS
#define LOGE(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_ERROR, tag, LOG_FORMAT(E, format), esp_log_timestamp(), tag, ##__VA_ARGS__)
#define LOGW(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_WARN, tag, LOG_FORMAT(W, format), esp_log_timestamp(), tag, ##__VA_ARGS__)
#define LOGI(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_INFO, tag, LOG_FORMAT(I, format), esp_log_timestamp(), tag, ##__VA_ARGS__)
#define LOGD(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_DEBUG, tag, LOG_FORMAT(D, format), esp_log_timestamp(), tag, ##__VA_ARGS__)
#define LOGV(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_VERBOSE, tag, LOG_FORMAT(V, format), esp_log_timestamp(), tag, ##__VA_ARGS__)
#else
#define LOGE(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_ERROR, tag, LOG_SYSTEM_TIME_FORMAT(E, format), esp_log_system_timestamp(), tag, ##__VA_ARGS__)
#define LOGI(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_INFO, tag, LOG_SYSTEM_TIME_FORMAT(I, format), esp_log_system_timestamp(), tag, ##__VA_ARGS__)
#define LOGW(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_WARN, tag, LOG_SYSTEM_TIME_FORMAT(W, format), esp_log_system_timestamp(), tag, ##__VA_ARGS__)
#define LOGD(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_DEBUG, tag, LOG_SYSTEM_TIME_FORMAT(D, format), esp_log_system_timestamp(), tag, ##__VA_ARGS__)
#define LOGV(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_VERBOSE, tag, LOG_SYSTEM_TIME_FORMAT(V, format), esp_log_system_timestamp(), tag, ##__VA_ARGS__)
#endif

namespace ReadieFur
{
    class Logging //: public Service::AService
    {
    private:
        static int FormatWrite(int (*writer)(char*, size_t), const char* format, va_list args)
        {
            //Based on esp32-hal-uart.c::log_printfv
            
            static char preallocBuffer[64]; //Static local buffer (persists across all calls, is faster than alloc each time).
            char *temp = preallocBuffer; //Pointer to buffer.
            // va_list args;
            uint32_t len;
            va_list copy;

            //Copy the argument list because vsnprintf modifies it.
            // va_start(args, format);
            va_copy(copy, args);

            //Determine the length of the formatted string.
            len = vsnprintf(NULL, 0, format, copy);
            va_end(copy);

            //If the formatted string exceeds the size of the static buffer, allocate memory dynamically.
            if (len >= sizeof(preallocBuffer))
            {
                //Allocate enough memory for the string and null terminator.
                temp = (char*)malloc(len + 1);
                if (temp == NULL)
                {
                    va_end(args); //End the variadic arguments if malloc fails.
                    return 0; //Return if memory allocation failed.
                }
            }

            //Format the string into the buffer.
            vsnprintf(temp, len + 1, format, args);

            int written = writer(temp, len);

            //Free dynamically allocated memory if it was used.
            if (temp != preallocBuffer)
                free(temp);

            // va_end(args); //End the variadic arguments.

            return written;
        }

    public:
        static void Log(esp_log_level_t level, const char* tag, const char* format, ...)
        {
            //TODO: Fix this for * log level overrides.
            // esp_log_level_t localLevel = esp_log_level_get(tag);
            // if (level == ESP_LOG_NONE || level > localLevel)
            //     return;

            va_list args;
            va_start(args, format);

            esp_log_writev(level, tag, format, args); //TODO: Send to a logger that doesn't do any formatting as I do it manually above for the WebSerial call if enabled.

            esp_log_level_t localLevel = esp_log_level_get(tag);
            if (level == ESP_LOG_NONE || level > localLevel)
            {
                va_end(args);
                return;
            }

            #if defined(WebSerial) || defined(WebSerialLite)
            FormatWrite([](char* data, size_t len) { return (int)WebSerial.write((const uint8_t*)data, len); }, format, args);
            #endif

            va_end(args);
        }

        static int Write(const char *format, ...)
        {
            va_list args;
            va_start(args, format);

            int written = FormatWrite([](char* data, size_t len)
            {
                int lWritten = uart_write_bytes(0, data, len); //STDOUT is typically UART0.
                #if defined(WebSerial) || defined(WebSerialLite)
                WebSerial.write((const uint8_t*)data, len);
                #endif
                return lWritten;
            }, format, args);

            va_end(args); //End the variadic arguments.

            return written;
        }
    };
};
