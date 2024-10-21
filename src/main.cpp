#include "freertos/FreeRTOS.h" //Has to be included first.
#include "Service/ServiceManager.hpp"
#include "Event/Event.hpp"
#include "Event/AutoResetEvent.hpp"
#include "Event/CancellationToken.hpp"

#ifdef ARDUINO
void setup() {}
void loop() {}
#else
extern "C" void app_main() {}
#endif
