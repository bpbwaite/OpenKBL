# Agent notes

Open Keyboard Lights (okbl): an LED strip above an 88-key piano that lights in response to MIDI.
This file is the only place for agent notes. Don't add design docs, ADRs, specs or issue files.
Design decisions belong in code comments next to the code they explain, and plans live in the
conversation. [README.md](README.md) is for people using and building the device.

## Repo layout

```
keyboard-lights-firmware/   PlatformIO project (ESP32-C3, Arduino framework)
  platformio.ini            envs: dev_ (debug flags), release_, brightness_floor (hardware test)
  partitions.csv            pinned flash layout; nvs holds Saved Settings, don't move it
  src/                      all firmware, flat, one module per .h/.cpp pair
  src/tests/                on-device hardware tests, excluded from the main build
  test/test_hands/          PC tests for hands.cpp, plain g++, not PlatformIO's test runner
  dist/                     prebuilt release images for esptool
piano-pcb/                  KiCad 10 project (schematic, PCB, BOM; gerbers are gitignored)
models/                     3D printable case
```

Where things go in `src/`:

- `hardware.h`: values fixed by the PCB or the piano (pins, LED count, key range), per
  `PCB_REVISION`. Don't make these tunable.
- `app_conf.h`: every tunable value, grouped in one namespace per area (`lights`, `menu`,
  `hands`, …). Put new constants here, not in the module that uses them.
- `settings.h`: the Settings enums and struct. Its header comment has the checklist for adding a
  Setting or an Option. Follow it and keep it up to date.
- `settings_menu.cpp`: the `GROUPS` table. It is the single source for the menu layout, the
  serial `set`/`settings` commands and the debug log.
- `render.cpp`: turns Key state plus Settings into LED targets. `main.cpp` does input, frame
  timing, brightness and power limiting.
- `hands.cpp`: pure C++ with no Arduino includes, so it can be tested on the PC. Keep it that way.

## Build, test, flash

Run these from `keyboard-lights-firmware/`. If `pio` isn't on PATH, it's at
`~/.platformio/penv/Scripts/pio` (Windows).

- `pio run -e dev_seeed_xiao_esp32c3` builds the default env.
- `pio run -e release_seeed_xiao_esp32c3` builds what users flash.
- `pio run -e brightness_floor` builds the hardware test. It shares `rotary_encoder.cpp`, so
  build it after changing that file.
- Hand tracker tests (any host g++, or under `wsl`):
  `g++ -std=c++17 -Wall -Isrc test/test_hands/test_hands.cpp src/hands.cpp -o test_hands && ./test_hands`.
  Run them after any change to `hands.*`. The test's `CONFIG` mirrors `okbl::hands` in
  `app_conf.h`, so keep the two in step.

Before calling a change done, build the dev and release envs. Most behaviour can only be checked
on hardware, so say what you couldn't check. With `-DKBL_USB_MIDI_DEBUG`, the USB serial console
accepts commands (`help`, `on <note>`, `set <setting> <option>`, `menu`, …) for testing without a
piano.

To refresh `dist/` after a release build, use the esptool that PlatformIO installed
(`~/.platformio/packages/tool-esptool/esptool`, v5). The offsets match `pio run -t upload`:

```
B=.pio/build/release_seeed_xiao_esp32c3
esptool --chip esp32c3 merge-bin -o dist/okbl-firmware-merged.bin --flash-mode dio --flash-freq 80m --flash-size 4MB \
  0x0 $B/bootloader.bin 0x8000 $B/partitions.bin \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 $B/firmware.bin
cp $B/firmware.bin dist/okbl-firmware-app.bin
```

## Compatibility traps

- **Saved Settings store Option indexes.** Reordering or removing an Option changes what boards
  already saved. Add new Options before `COUNT`, at the end of their enum.
- **The NVS namespace is `"kbl-settings"`** (settings_store.cpp). It predates the kbl→okbl rename.
  Renaming it wipes every board's Saved Settings.
- **The menu layout is worked out from `GROUPS` order.** A group inserted in the middle moves
  every group after it up the keyboard, so append new groups at the end.
- **Prefer an Option over a new Setting.** An Option costs one menu key. A new Setting costs a
  group of keys plus a gap, and two Settings whose combinations don't all make sense are worse
  than one Setting with more Options.

## Code style

- Formatting comes from [keyboard-lights-firmware/.clang-format](keyboard-lights-firmware/.clang-format)
  (4-space indents, 160 columns, indented namespaces). Some existing code predates it, so format
  the code you change (`clang-format -i <file>`) and don't reformat untouched files in the same
  change. The config doesn't sort includes, so order them by hand.
- Everything lives in `namespace okbl`, and file-local helpers go in an anonymous namespace.
  Debug macros keep the old `KBL_` prefix.
- Constants are `static const` in `UPPER_SNAKE` (in `app_conf.h` or `hardware.h`), functions and
  variables are `camelCase`, and types and enum values are `PascalCase`. Use `enum class ... :
  uint8_t` with a trailing `COUNT`, and `static_assert` that tables match `COUNT`.
- Use `#pragma once`. Include the module's own header first, then project headers.
- Comments say what a value means and why it is what it is, in plain sentences. Include units
  (`_MS`, `_S`, "per frame"). Explain a non-obvious choice where it's made, including what was
  tried and why it didn't work, when that stops someone repeating it. Don't keep changelogs or
  dates in comments.
- Keep `main.cpp` thin. New behaviour goes in its own module or in `render.cpp`.

## Glossary

Use these terms in code comments, serial output, commit messages and replies. Capitalize them as
shown so it's clear the defined sense is meant. Keep the glossary current:

- Add a term when a new concept needs a name that people or code will reuse. Don't add a term for
  something only one function uses.
- Every term has one meaning. If you reach for a synonym, use the term instead. If the glossary
  doesn't fit, change the glossary rather than drifting.
- List likely confusions under _Avoid_.
- When a feature is removed or a concept is renamed, update or delete its entry, and rename the
  code to match.
- Define what a term means today, not how it got there.

### Piano

**Key**:
One of the 88 physical piano keys.
_Avoid_: Note (when you mean the physical key)

**Note**:
A MIDI event or pitch, as received from the piano.
_Avoid_: Key (when you mean the event or pitch)

**PitchClass**:
One of the twelve pitches C through B, with no octave.
_Avoid_: Note name

**Held**:
A Key that is physically down.

**Sustained**:
A Key that has been released while the sustain pedal keeps it going.

**Sounding**:
A Key that is either Held or Sustained.
_Avoid_: Lit (of a Key)

**Hand**:
The left or right hand, as worked out from the order of Notes for Color::Hands. Every Held Key
belongs to exactly one.

**Split Point**:
The Key where, with nothing else to go on, the right Hand begins: C4, so B3 and below are left.

### Lights

**LED**:
The light assigned to one Key. An LED can be lit without its Key being pressed.

**Fading**:
An LED dimming to off after nothing asks it to stay lit. Applies to LEDs only, never to Keys.

**Setting**:
One independent choice that shapes how LEDs respond to Sounding Keys, such as Color or
Brightness. It always holds exactly one value.
_Avoid_: Mode (for anything other than Light Mode), category

**Light Mode**:
The Setting that decides which LEDs light for the Sounding Keys, such as only the pressed Key or
every octave of its PitchClass.
_Avoid_: Color mode

**Pedal Fade**:
A Setting under which Sustained Keys dim out and go dark a fixed time after release, instead of
staying fully lit while the pedal is down.

**Blank On Press**:
A Setting under which pressing a Key whose LED is already lit (it is Sounding, or still Fading)
turns that LED off for a brief holdoff before it lights again, so every press shows.

**Accent**:
A Brightness Option: brightness follows velocity as under Velocity, and a Note pressed much harder
than the Notes before it flashes white, then settles into its color.

**Base Hue**:
The hue every LED shows under the BaseHue Color. While that Color is selected, the rotary encoder
turns the Base Hue instead of Master Brightness.

**Overtone**:
A Key above a Sounding Key that the Harmonics Light Mode also lights, dimmer: the octave, the
octave and a fifth, and two octaves.

**Saved Settings**:
The Settings as last written to the board's flash, loaded at power-on. They are written when the
Settings Menu closes, at most once every 30 seconds.

### Control

**Master Brightness**:
The overall brightness of all LEDs, set with the rotary encoder. It can never reach fully off.

**Settings Menu**:
A state, opened with the encoder switch, in which white Keys at the bass end choose Settings
instead of lighting up.
_Avoid_: Mode Select

**Option**:
One value of a Setting, assigned to one white Key in the Settings Menu. An on/off Setting has two
Options, Off and On.
_Avoid_: Toggle

**Menu Action**:
A Key in the Settings Menu that does something once instead of choosing a value, such as starting
Demo Mode.

**Swatch**:
The small animation an Option's LED shows in the Settings Menu, bright when selected and dim when
not.

**Preview**:
A brief display on all LEDs that shows how the current Settings look.
_Avoid_: Mode Preview

**Demo Mode**:
A state in which LEDs light at random under the current Settings with no Keys pressed, until
someone plays. It is not a Light Mode.
