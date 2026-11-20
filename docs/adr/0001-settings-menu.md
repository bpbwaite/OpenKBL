---
status: accepted
---

# Settings are chosen from a Settings Menu on the keys, not from Mode Select chords

Chord-based Mode Select gives one chord per Light Mode, and each new variant of a mode (pedal
behaviour, hue drift, …) would need another chord. There aren't enough memorable chords, and a
chord gives no feedback until it has been played. Instead, the encoder switch opens a **Settings
Menu**: each Key is one **Option**, Options are grouped into **Settings** that behave like radio
buttons, and the LED above each Key shows what that Key does and whether it is selected. The
current single `LightMode` enum is split into independent Settings so variants multiply instead of
each needing its own enum value. Mode Select chords are removed. Presets and saving to NVS are
deliberately left for later.

## Considered Options

- **Keep chords, add more.** Rejected: runs out of memorable chords and gives no feedback.
- **One octave per Setting.** Rejected: wastes an octave on a Setting with two values and runs out
  of octaves once Settings gain their own parameter Settings (e.g. hue-drift start/stop/speed).
- **Fixed key ranges (modes at C2–C4, settings at C5–C6).** Rejected: every new Setting has to be
  hand-placed, and the ranges fill up.
- **Chosen: Settings packed onto white keys from A0 upward, with one unlit white key between
  groups.** Each Setting takes one white key per Option, and an on/off Setting has two Options (Off
  and On) like any other radio group. Only a Menu Action takes a single key. Using white keys only
  means every Setting can be changed without reaching for a sharp or flat, and it keeps the
  distance between groups the same everywhere. The layout is worked out from the menu table, so
  adding a Setting means adding a table entry. The LEDs label everything, so the layout doesn't need
  to be memorised.

## Consequences

- Adding a Setting in the middle of the table moves every Setting after it up the keyboard. Add new
  Settings at the end unless moving the others is acceptable.
- Some Options (a Light Mode, say) can't be fully shown by one animated LED. Pressing any Option
  also shows a short full-strip **Preview** of the result, so the Swatch only needs to tell Options
  apart. The full Preview shows what each one actually does.
- The menu sits at the bass end, right below the encoder (which is just above A0's LED).

---

# Implementation plan

## Domain model

| Term | Meaning |
|---|---|
| **Settings Menu** | A state, opened with the encoder switch, in which Keys choose Settings instead of lighting up. |
| **Setting** | One independent choice that shapes how LEDs respond, e.g. Color or Light Mode. Holds exactly one value. |
| **Option** | One value of a Setting, assigned to one Key in the Settings Menu. |
| **Menu Action** | A Key in the Settings Menu that does something once instead of choosing a value, e.g. start Demo Mode. |
| **Swatch** | The animated color an Option's LED shows in the Settings Menu. |
| **Preview** | (was Mode Preview) A brief full-strip display of how the current Settings look. |

**Light Mode** narrows to one Setting: *which* LEDs light for the Sounding Keys (the pressed key,
every octave, later ripples, …). Color and brightness become their own Settings.

## Settings after the split

Every current mode is a combination of these Settings, so the split loses nothing:

| Setting | Kind | Options |
|---|---|---|
| Light Mode | radio | `PressedKey`, `AllOctaves` |
| Color | radio | `BaseHue`, `PitchHue`, `KeyColors` |
| Brightness | radio | `Velocity`, `Full` |
| Pedal Fade | radio | `Off`: Sustained Keys stay fully lit while the pedal is down (today's behaviour); `On`: Sustained Keys dim out and are off `SUSTAIN_FADE_MS` (10 s) after release |

| Old `LightMode` | New Settings |
|---|---|
| VelocityBrightness | PressedKey · BaseHue · Velocity |
| FullBrightness | PressedKey · BaseHue · Full |
| PitchHue (the default) | PressedKey · PitchHue · Velocity |
| KeyColors | PressedKey · KeyColors · Velocity |
| AllOctaves | AllOctaves · KeyColors · Velocity |

Pedal Fade is the first new Setting. It's separate from the normal
per-frame Fading: that one runs after a key stops Sounding, while this one dims a key that is still
Sounding.

## Default layout (worked out from the table)

```
white key: A0   │ B0  │ C1  D1    │ E1  │ F1  G1  A1 │ B1  │ C2  D2     │ E2  │ F2  G2
           Demo │ gap │ LightMode │ gap │ Color      │ gap │ Brightness │ gap │ PedalFade
```

Keys above the last Option (G#2 and up here) are *outside the menu*. Gap keys and black keys are
inside it and ignored, so a near miss doesn't close the menu.

That's 15 of the 52 white keys. When the Color Setting gains `HueDrift`, its speed or start/end hues become
new Settings appended at the end.

## Code structure

### `settings.h` (replaces `light_modes.h`)

The data only: one enum per radio Setting, and a plain struct that the render code reads directly.

```cpp
enum class LightMode : uint8_t { PressedKey, AllOctaves, COUNT };
enum class Color     : uint8_t { BaseHue, PitchHue, KeyColors, COUNT };
enum class Brightness: uint8_t { Velocity, Full, COUNT };

struct Settings {
    LightMode lightMode;
    Color color;
    Brightness brightness;
    PedalFade pedalFade;   // enum class PedalFade { Off, On, COUNT }
};
```

A short "adding a Setting" checklist at the top of the file: (1) add the enum and field here,
(2) handle it in `render.cpp`, (3) add a group to the menu table in `settings_menu.cpp`,
(4) give it a default in `app_conf.h`.

### `render.{h,cpp}` (rendering moved out of `main.cpp`)

`computeTargets(CRGB *target, const KeyState *keys, const Settings &s, uint32_t nowMs, uint8_t floorPeak)`
runs in two steps, so the Settings stay independent:

1. **Light Mode** decides which LEDs are lit and how hard, filling one `Lit` per LED: a velocity
   (0 = unlit), whether it is fading under Pedal Fade (Sustained, not held, Pedal Fade on), and
   for how long. `PressedKey` copies each Key's own values. `AllOctaves` gives every Key of a
   Sounding PitchClass the hardest velocity in that class; the class fades only while none of its
   Keys is held, timed from the most recent release.
2. **Shade** each lit LED: `colorOf(ledIndex, brightnessOf(velocity, s), s)` (Color and Brightness
   Settings), then Pedal Fade scales the finished color with FastLED's `nscale8_video`. Fading
   the finished color means Pedal Fade runs at the same rate under every Brightness and Color.
   Scaling the brightness instead would put FastLED's CHSV dimming curve on top of the fade for
   hue-based Colors (on hardware, the first 20 s fade went dark at about 16 s that way). The
   `_video` scale never takes a lit channel to zero, so the hue holds to the end.

**How Pedal Fade dims:** a fading LED loses brightness at one fixed rate, in halvings per second,
until its largest channel reaches `floorPeak`, the dimmest target that still shows at the current
global brightness. Then it turns off and the tail below the floor (below) takes it out.
- **The rate** is set per LED so that its color at full brightness would reach the floor after
  `SUSTAIN_FADE_MS` (10 s): `log2(fullPeak / floorPeak)` halvings over that time. A full-velocity
  key therefore takes 10 s in any Color. (Aqua's brightest channel at full is 169, so a rate based
  on 255 ended a full-velocity aqua key at about 8.5 s.) Because the fade aims at the real floor,
  it uses the whole visible range at any Master Brightness instead of sitting at the floor before
  cutting out.
- **Softer keys go dark sooner.** Every LED fades at the same rate, so under `Brightness::Velocity`
  (the default) a softly pressed key starts closer to the floor and gets there first. Under
  `Brightness::Full` every key takes `SUSTAIN_FADE_MS`.
- **A minimum hold.** Every Sustained Key stays lit for at least `SUSTAIN_FADE_MIN_MS` (1.5 s),
  held at the floor if need be. Without it, a key that starts at or below the floor (a soft key,
  or any key at low Master Brightness) went dark the moment it was released, so the pedal didn't
  visibly hold it at all.
- **The floor** comes from `main.cpp`, worked out from the last frame's global brightness (this
  frame's isn't known until it's rendered). At the lowest Master Brightness the floor is close to
  full, which leaves almost no room to fade, so the timing treats the floor as at most half
  brightness. The LED then sits at the floor and goes dark at the time its velocity sets.
- **Why the rate is fixed:** equal halvings per second look like an even fade to the eye. A linear
  fade seems to barely change and then drop off at the end.
- **Earlier versions:** a squared curve over 20 s, then a fixed 4 halvings over 10 s. Both ended
  at a fixed fraction of the starting brightness, not at the floor. Depending on Master Brightness
  the LED could sit at the floor for seconds before cutting out, which looked like it just switched
  off.

To support this, `KeyState` gains `releasedMs`, which `handleNoteOff` sets.

**The tail below the floor:** once a fading LED drops below the brightness floor, it fades by an
extra `BELOW_FLOOR_FADE_PER_FRAME` each frame instead of cutting straight to black, and that frame
goes out with dithering off. This applies to every fading LED, not just Pedal Fade. The tail lasts
a few tens of milliseconds at medium-high Master Brightness and about 100 ms at the lowest, which is
too short for the flicker below the floor to be seen.

Each Light Mode also supplies its own preview keys, `previewKeys(LightMode, KeyState *)`. Today
that's a velocity ramp across the strip, or a C-E-G chord for AllOctaves. This replaces the
`switch` in `showModePreview()`.

`keyPitchClass()` and `velocityBrightness()` move here from `main.cpp`.

### `settings_menu.{h,cpp}` (new)

Public interface:

```cpp
namespace kbl::menu {
    void open();
    void close();
    bool isOpen();
    void toggle();                         // encoder press
    bool onKeyDown(uint8_t keyIndex);      // only called while open; false = Key is outside the
                                           // menu, which closed, so play it as a normal note
    void poll();                           // timeout
    void render(CRGB *target, const Settings &s, uint32_t nowMs);
}
```

The menu is **one table**, and its order is the layout order:

```cpp
using Swatch = CRGB (*)(const Settings &s, uint32_t nowMs);

struct Option { const char *name; Swatch swatch; };

enum class GroupKind : uint8_t { Radio, Action };

struct MenuGroup {
    const char *name;
    GroupKind kind;
    const Option *options;
    uint8_t optionCount;                   // Action: 1
    uint8_t &(*value)(Settings &);         // Radio only; nullptr for Action
    void (*action)();                      // Action only
};

const Option COLOR_OPTIONS[] = {
    { "BaseHue",   swatch::baseHue },
    { "PitchHue",  swatch::rainbowCycle },
    { "KeyColors", swatch::keyColorCycle },
};
static_assert(ARRAY_SIZE(COLOR_OPTIONS) == (size_t)Color::COUNT, "one Option per Color");

const MenuGroup GROUPS[] = {
    { "Demo",       GroupKind::Action, DEMO_OPTION,       1, nullptr, kbl::demo::start },
    { "LightMode",  GroupKind::Radio,  LIGHT_MODE_OPTIONS, ARRAY_SIZE(LIGHT_MODE_OPTIONS),
      [](Settings &s) -> uint8_t & { return (uint8_t &)s.lightMode; }, nullptr },
    ...
};
```

- **The value accessor** is a captureless lambda per group. It's plain C++11 and works the same way
  for every Setting's enum.
- **The layout** is worked out once at boot into `int8_t keyGroup[NUM_LEDS]` and
  `int8_t keyOption[NUM_LEDS]` (−1 = gap, black key or unused). It steps over white keys only,
  leaving `GAP_WHITE_KEYS` white keys between groups. If the table doesn't fit on the keyboard, or
  `FIRST_KEY` is a black key, boot prints an error and halts.
- **`onKeyDown`** does: Radio → `value(s) = option`, Action → `close(); action();`. After a Radio
  press it shows a Preview and restarts the timeout.
- **Keys outside the menu** (above the last Option): `onKeyDown` closes the menu and returns
  `false`. `main.cpp` then handles the Key as a normal note, so you can just start playing.
- **`render`** puts each Option's Swatch on its LED and leaves gaps and unused LEDs black. Selected
  Options (a Radio's current value, or any Action) show at full brightness. Unselected ones are
  scaled by `UNSELECTED_SCALE`.
- **Settings live in `main.cpp`** as `Settings settings = DEFAULT_SETTINGS;`. The menu reaches them
  through `getSettings()`/`onSettingsChanged()` declared in its header, the same pattern
  `demo_mode.h` uses for `onDemoModeStart()`.

### Swatches (how the LEDs show what each Option is)

A Swatch is a function of the current Settings and time, so it can animate and use the current
color. These are first proposals; each is a one-line function, so they're cheap to change:

| Option | Swatch |
|---|---|
| Color · BaseHue | steady BASE_HUE |
| Color · PitchHue | slowly cycles through the rainbow |
| Color · KeyColors | steps through the 12 PitchClass colors, about 0.4 s each |
| Brightness · Velocity | sawtooth ramp, dim to full, in the current color |
| Brightness · Full | steady full, in the current color |
| Light Mode · PressedKey | single "pluck" (flash, then fade) every second |
| Light Mode · AllOctaves | three quick plucks in a row, like one key echoing up the octaves |
| Pedal Fade · Off | strike that settles and stays lit |
| Pedal Fade · On | strike that holds, then dims out |
| Demo | random twinkle |

### Input and rendering in `main.cpp`

- **Rendering priority** in `renderKeys()`: Preview > Settings Menu > live/Demo keys. The fade,
  sag compensation, power limit and brightness floor apply to all of them as they do now.
- **Menu brightness:** while the menu is open, the global brightness starts from
  `max(masterScale, menu::MIN_GLOBAL_BRIGHTNESS)`. Selected and dimmed Options stay distinguishable even
  when Master Brightness is at its lowest. Power limiting still applies on top.
- **`handleNoteOn`**: if the menu is open and `menu::onKeyDown(i)` returns `true`, return without
  touching `keyStates`. Note-off and pedal events pass through as usual, which is harmless.
- **Encoder press** calls `menu::toggle()`. Opening the menu runs `clearKeys()`, `demo::stop()` and
  `demo::recordActivity()`. **Encoder turn** still adjusts Master Brightness, menu open or not.
- **`loop()`** gains `menu::poll()` for the timeout.

### `app_conf.h`

- **Delete** the `chords` namespace and `DEFAULT_LIGHT_MODE`.
- **Add** `lights::DEFAULT_SETTINGS`, which keeps today's default look:
  PressedKey · PitchHue · Velocity · Pedal Fade off.
- **Add** `lights::SUSTAIN_FADE_MS = 10000` (how long a full-velocity key takes to reach the
  floor), `lights::SUSTAIN_FADE_MIN_MS = 1500` and `lights::BELOW_FLOOR_FADE_PER_FRAME`.
- **Add** a `menu` namespace with `FIRST_KEY = 0` (A0), `GAP_WHITE_KEYS = 1`, `TIMEOUT_MS = 30000`,
  `UNSELECTED_SCALE`, `MIN_GLOBAL_BRIGHTNESS`, and swatch timing constants.

### `serial_commands.h`

- **Replace** `mode` with `set <setting> <option>`, e.g. `set color keycolors` or
  `set pedalfade on`. Names are matched case-insensitively.
- **Add** `settings`, which lists every Setting with its Options and marks the current one.
- **Add** `menu`, which acts like an encoder press. With the existing `on <note>` command, the whole
  menu can be driven over USB.
- **Generate** help text and name lookup from the menu table, so the table stays the single source
  of truth.

### Removed

- **Deleted files:** `chord_modes.h` and `light_modes.h`.
- **Deleted from `main.cpp`:** `modeSelectArmed`, `modeSelectKeyDownMs`, `heldChord()`,
  `checkModeChord()`, `keyColor()`, `renderAllOctaves()` and the per-mode preview `switch`.
- **Stale comments:** the "rotary press" mention in `demo_mode.h`.
- **Glossary:** "Mode Select" leaves `CONTEXT.md`.

## Steps

Each step builds and can be checked on hardware on its own.

1. **Settings model and render split.** Add `settings.h` and `render.*`. Delete
   `light_modes.h`, `chord_modes.h` and the Mode Select code. Replace `mode` with
   `set`/`settings`. *Check:* set each of the five old combinations over serial and confirm it
   looks the same as before.
2. **Settings Menu.** Layout, open/close (encoder, timeout, and a Key outside the menu), radio and
   Action input, static Swatches (selected vs dimmed), and the menu's minimum brightness. The Demo
   Action goes on A0 and the encoder no longer starts Demo Mode directly. *Check:* open, select,
   time out, and close by playing above the menu, all at the lowest Master Brightness, on hardware
   and over serial `menu`.
3. **Animated Swatches and press Preview.** Generalise `showModePreview()` to
   `showPreview()` using each Light Mode's preview keys.
4. **Pedal Fade.** *Check:* with Pedal Fade on, hold the pedal and play. Released keys should
   dim smoothly and go dark at about 10 s for a full-velocity key, sooner for softer ones (but
   not before 1.5 s), with no
   visible steps or flicker near the floor. With it
   off, they should behave as today.
5. **Docs.** Apply the glossary changes above to `CONTEXT.md` and mark this ADR accepted.

*Each step:* `pio run -e seeed_xiao_esp32c3` and `pio run -e brightness_floor`. The second only
shares the encoder code, but it guards against breaking the shared code.

## Resolved in review

1. **Low Master Brightness:** the menu has a minimum brightness, `menu::MIN_GLOBAL_BRIGHTNESS`, set in
   code rather than in the menu.
2. **On/off Settings:** these are radio groups with two Options, Off and On, not single-key Toggles.
   (Revised after the first hardware review; the first version had a one-key Toggle kind.)
3. **Pedal Fade:** Sustained Keys dim out and are fully off after `SUSTAIN_FADE_MS`, which is
   configurable. It was first 20 s on a squared curve. After the hardware review it fades at a
   fixed rate to the real floor at the current Master Brightness: 10 s for a full-brightness key,
   sooner for softer keys, then a quick tail below the floor.
4. **Timeout and closing:** the timeout is 30 s. A Key above the menu closes it and plays as a
   normal note; gap keys and black keys are ignored.
5. **White keys only:** Options sit on white keys, with one white key between groups (revised after
   the first hardware review; the first version used every key, with a 2-key gap).
