#include <Arduino.h>

// serial debug interface

#define BAUDRATE_ESP_SERIAL (115200)

#define debugStart() Serial.begin(BAUDRATE_ESP_SERIAL)
#define debugWaitForInterface() {while (!Serial) { yield(); }}

#define debugf(...) Serial.printf(__VA_ARGS__)