#include <Arduino.h>
#include <FastLED.h>

#include "app_conf.h"
#include "hardware.h"

CRGB kblLEDs[kbl::leds::NUM_LEDS];

void setup() {

  debugStart();
  debugWaitForInterface();
  
  debugf("Serial Initialized");
  
  // connected to high side of optocoupler w/ external pullup.
  // todo: does library configure?
  pinMode(kbl::pins::MIDI_RX, INPUT); 
  
  pinMode(kbl::pins::ROTARY_A, INPUT);
  pinMode(kbl::pins::ROTARY_B, INPUT);
  pinMode(kbl::pins::ROTARY_SW, INPUT);

  pinMode(kbl::pins::ENABLE_PSU, OUTPUT);
  // pinMode(kbl::pins::RGB_DATA, OUTPUT); // configured by fastled

  digitalWrite(kbl::pins::ENABLE_PSU, HIGH);
  
  debugf("Pinout Initialized");

  
  FastLED.addLeds<WS2812B, kbl::pins::RGB_DATA, EOrder::GRB>(kblLEDs, kbl::leds::NUM_LEDS);
  FastLED.setMaxPowerInMilliWatts(kbl::leds::PSU_MAX_MA * kbl::leds::PSU_VOLTAGE);

  debugf("LEDS Initialized");
}

void loop() {

  FastLED.show();

  EVERY_N_SECONDS(2) {
    debugf("FastLED at %d fps\n", FastLED.getFPS());
  }

  
  
  
}
