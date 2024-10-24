#pragma once

// #include "Service/AService.hpp"
#include <esp_log.h>
#include <driver/uart.h>
#if defined(WebSerial)
#include <WebSerial.h>
#elif defined(WebSerialLite)
#include <WebSerialLite.h>
#endif
#ifdef ARDUINO
#include <esp32-hal-log.h>
#endif
#include <stdio.h>

#define PRINT(format, ...) ReadieFur::Logging::Print(format, ##__VA_ARGS__)
#define WRITE(c) ReadieFur::Logging::Write(c)

#ifdef ARDUINO
#define ARDUHAL_LOG_FORMAT2(letter, format) ARDUHAL_LOG_COLOR_ ## letter "[%6u][" #letter "][%s:%u]: " format ARDUHAL_LOG_RESET_COLOR "\r\n", (unsigned long) (esp_timer_get_time() / 1000ULL), pathToFileName(__FILE__), __LINE__
#define LOGE(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_ERROR, tag, ARDUHAL_LOG_FORMAT2(E, format), ##__VA_ARGS__)
#define LOGW(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_WARN, tag, ARDUHAL_LOG_FORMAT2(W, format), ##__VA_ARGS__)
#define LOGI(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_INFO, tag, ARDUHAL_LOG_FORMAT2(I, format), ##__VA_ARGS__)
#define LOGD(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_DEBUG, tag, ARDUHAL_LOG_FORMAT2(D, format), ##__VA_ARGS__)
#define LOGV(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_VERBOSE, tag, ARDUHAL_LOG_FORMAT2(V, format), ##__VA_ARGS__)
#else
#define LOGE(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_ERROR, tag, LOG_FORMAT(E, format), esp_log_timestamp(), tag, ##__VA_ARGS__)
#define LOGW(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_WARN, tag, LOG_FORMAT(W, format), esp_log_timestamp(), tag, ##__VA_ARGS__)
#define LOGI(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_INFO, tag, LOG_FORMAT(I, format), esp_log_timestamp(), tag, ##__VA_ARGS__)
#define LOGD(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_DEBUG, tag, LOG_FORMAT(D, format), esp_log_timestamp(), tag, ##__VA_ARGS__)
#define LOGV(tag, format, ...) ReadieFur::Logging::Log(ESP_LOG_VERBOSE, tag, LOG_FORMAT(V, format), esp_log_timestamp(), tag, ##__VA_ARGS__)
#endif

namespace ReadieFur
{
    class Logging //: public Service::AService
    {
    private:
        static int FormatWrite(int (*writer)(const char*, size_t), const char* format, va_list args)
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
            esp_log_level_t localLevel = esp_log_level_get(tag);
            if (level == ESP_LOG_NONE || level > localLevel)
                return;

            va_list args;
            va_start(args, format);

            esp_log_writev(level, tag, format, args); //TODO: Send to a logger that doesn't do any formatting as I do it manually above for the WebSerial call if enabled.

            #if defined(WebSerial) || defined(WebSerialLite)
            FormatWrite([](const char* data, size_t len) { return (int)WebSerial.write(reinterpret_cast<const uint8_t*>(data), len); }, format, args);
            #endif

            va_end(args);
        }

        static int Write(char c)
        {
            int lWritten = putchar(c);
            #if defined(WebSerial) || defined(WebSerialLite)
            WebSerial.write(c);
            #endif
            return lWritten;
        }

        static int Print(const char* format, ...)
        {
            va_list args;
            va_start(args, format);

            int written = FormatWrite([](const char* data, size_t len)
            {
                // int lWritten = uart_write_bytes(0, data, len); //STDOUT is typically UART0 (only works if initalized).
                int lWritten = puts(data);
                #if defined(WebSerial) || defined(WebSerialLite)
                WebSerial.write(reinterpret_cast<const uint8_t*>(data), len);
                #endif
                return lWritten;
            }, format, args);

            va_end(args); //End the variadic arguments.

            return written;
        }
    };
};
