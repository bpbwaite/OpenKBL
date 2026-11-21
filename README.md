# Brie's Keyboard Lights

LED strip for an 88-key piano: each key's LED lights up while it's played, and the sustain pedal keeps released keys lit.

Hardware: custom KiCad PCB ([piano-pcb/](piano-pcb/)) with a Seeed XIAO ESP32-C3, an optocoupled MIDI input, a rotary encoder, and an 88-LED WS2812B strip.

Firmware: PlatformIO + FastLED + MIDI Library ([keyboard-lights-firmware/](keyboard-lights-firmware/)). How the LEDs respond is set by independent Settings: Light Mode (the pressed key, or every octave), Color (one hue, a rainbow across the keyboard, per-PitchClass colors set in `app_conf.h`, or Hands: the left hand blue and the right hand green), Brightness (by velocity, or full), and Pedal Fade (Sustained keys dim out to the brightness floor: 10 s for a full-velocity key, sooner for softer ones). It starts on the pressed key, rainbow, by velocity, with Pedal Fade off.

The rotary encoder sets the overall brightness. Its switch opens the Settings Menu: the white keys from A0 up each become one Option, grouped by Setting with one unlit white key between groups (A0 Demo, C1-D1 Light Mode, F1-B1 Color, D2-E2 Brightness, G2-A2 Pedal Fade), and each LED shows a small animation of its Option, bright when selected and dim when not. Press a key to choose that Option; the LEDs briefly preview the result. A0 starts Demo Mode, which lights keys at random under the current Settings. The menu closes on the encoder switch, after 30 s without a key press, or when you play a key above it. See [docs/adr/0001-settings-menu.md](docs/adr/0001-settings-menu.md).

Under Hands, the firmware works out which hand pressed each key from the order of the notes alone, never their timing (`src/hands.cpp`). Each hand reaches at most an eleventh, and fingers 2-5 at most an octave beside the thumb, so a key out of a hand's reach belongs to the other hand, even when the hands cross. A hand holds at most five keys, or six when the thumb takes two neighbours. Close calls can be corrected by later keys, and near ties go to the hand last played at the nearer velocity. Otherwise a key goes to whichever hand would move less to reach it, and with nothing to go on, B3 and below are the left hand and C4 and up the right (`kbl::hands` in `app_conf.h`). When a later key shows that a held key's hand was guessed wrong, that key changes color. A hand already holding a chord is less likely to take another key. If the hands are left crossed when every key is up, they swap places. Building with `-DKBL_HAND_DEBUG` makes corrected keys flash red. The PC tests are in `test/test_hands/` (build line at the top of the file).

Demo Mode also starts after 5 minutes without playing, and it stops on any key press or the sustain pedal. Starting it clears any stuck keys. Nothing is saved across reboots.

Set `-DPCB_REVISION` in `platformio.ini` to match the board; revision 0 needs the internal pullup on the encoder switch.

For testing without a piano, build with `-DKBL_USB_MIDI_DEBUG` and type commands like `on C4 E4 G4` into the USB serial monitor (`help` lists them).

