# Brie's Keyboard Lights

LED strip for an 88-key piano: each key's LED lights up while it's played, and the sustain pedal keeps released keys lit.

Hardware: custom KiCad PCB ([piano-pcb/](piano-pcb/)) with a Seeed XIAO ESP32-C3, an optocoupled MIDI input, a rotary encoder, and an 88-LED WS2812B strip.

Firmware: PlatformIO + FastLED + MIDI Library ([keyboard-lights-firmware/](keyboard-lights-firmware/)), with five Light Modes (per-PitchClass colors are set in `app_conf.h`). It starts in PitchHue. To change mode, press A0 on its own, hold it for at least 100 ms, and play a chord; the chords are set in `app_conf.h`. The LEDs briefly show a preview of the new mode.

The rotary encoder sets the overall brightness, and its switch starts Demo Mode, which lights keys at random under the current Light Mode. Demo Mode also starts after 5 minutes without playing, and it stops on any key press or the sustain pedal. Starting it clears any stuck keys. Nothing is saved across reboots.

Set `-DPCB_REVISION` in `platformio.ini` to match the board; revision 0 needs the internal pullup on the encoder switch.

For testing without a piano, build with `-DKBL_USB_MIDI_DEBUG` and type commands like `on C4 E4 G4` into the USB serial monitor (`help` lists them).
