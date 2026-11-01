#pragma once

#include <Arduino.h>
#include <MIDI.h>

#include "hardware.h"

class MidiSerialTransport {
public:
    explicit MidiSerialTransport(HardwareSerial& serial) : serial_(serial) {}

    void begin(unsigned long baud) {
        serial_.begin(baud, SERIAL_8N1, kbl::pins::MIDI_RX, -1);
    }

    byte read() {
        return serial_.read();
    }

    void write(byte value) {
        serial_.write(value);
    }

    unsigned available() {
        return serial_.available();
    }

private:
    HardwareSerial& serial_;
};

MidiSerialTransport midiSerialTransport(Serial1);
MIDI_CREATE_INSTANCE(MidiSerialTransport, midiSerialTransport, MIDI);