#pragma once

#include <Arduino.h>

// serial debug interface

#define BAUDRATE_ESP_SERIAL (115200)
#define DEBUG_WAIT_MS (3000) // how long to wait for a serial monitor at boot

// Serial is the C3's USB CDC (HWCDC). With a 0 ms TX timeout, prints are dropped
// instead of blocking when the host isn't reading (e.g. USB plugged, monitor closed).
#define debugStart() {Serial.begin(BAUDRATE_ESP_SERIAL); Serial.setTxTimeoutMs(0);}
// bounded so the board still boots without a USB host
#define debugWaitForInterface() {unsigned long t0 = millis(); while (!Serial && millis() - t0 < DEBUG_WAIT_MS) { yield(); }}

#define debugf(...) Serial.printf(__VA_ARGS__)
