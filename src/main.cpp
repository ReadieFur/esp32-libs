#ifdef ARDUINO
void setup() {}
void loop() {}
#else
extern "C" void app_main() {}
#endif
