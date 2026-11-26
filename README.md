# Open Keyboard Lights

OpenKeyBoardLights (okbl) is an embedded project for MIDI piano keyboards. \
The device sits above the keys of an 88-key piano, and illuminates when keys are played. \
It's less expensive than some commercial options, and has more features!

*Technologies:* ESP32-C3, PlatformIO, KiCAD 10 - [Custom PCB](piano-pcb/), and a [3D printable case](models/)

## Usage

Revision 0:
- Turn the encoder to set the brightness.
- Press the encoder to open the settings menu
    - Press a lit key to change and preview its corresponding option.

## Roadmap

| Hardware Feature | Implemented |
|-|-|
| Accurate LED spacing | ✅ |
| Low resistance traces | ✅ |
| Newly improved PCB | 🚧 |
---
| Software Feature | Implemented |
|-|-|
| Various color modes | ✅ |
| Demo mode (5 min timer) | ✅ |
| Control over serial | ✅ |
| Control with desktop app | 🚧 |
| Control over WiFi | 🚧 |
| Training mode | 🚧 |
| Hand mode (blue-green) neural model | 🚧 |
---

## Flash Binary

With PlatformIO (recommended):
```
cd keyboard-lights-firmware/
pio run -e release_seeed_xiao_esp32c3 -t upload
```
With esptool v4+, using a prebuilt image:
```
esptool.py --chip esp32c3 --port COMx --baud 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 4MB 0x0 okbl-firmware-<version>-merged.bin

```

## Building

- Open [keyboard-lights-firmware/](keyboard-lights-firmware/) with PlatformIO
- Set `-DPCB_REVISION` in `platformio.ini` to match your board.
- For testing without a piano, build with `-DKBL_USB_MIDI_DEBUG`. Type `help` to list commands.

## Assembly

- still todo: BOM, gerbers

## Credits
- FastLED
- MIDI Library
