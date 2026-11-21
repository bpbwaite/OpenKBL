# Keyboard Lights

An LED strip for an 88-key piano that lights up in response to what is being played. This glossary covers the language of the piano, its lights, and how a player controls them.

## Language

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
The left or right hand, as worked out from the order of Notes for Color::Hands. Every Held Key belongs to exactly one.

**Split Point**:
The Key where, with nothing else to go on, the right Hand begins: C4, so B3 and below are left.

### Lights

**LED**:
The light assigned to one Key. An LED can be lit without its Key being pressed.

**Fading**:
An LED dimming to off after nothing asks it to stay lit. Applies to LEDs only, never to Keys.

**Setting**:
One independent choice that shapes how LEDs respond to Sounding Keys, such as Color or Brightness. It always holds exactly one value.
_Avoid_: Mode (for anything other than Light Mode), category

**Light Mode**:
The Setting that decides which LEDs light for the Sounding Keys, such as only the pressed Key or every octave of its PitchClass.
_Avoid_: Color mode

**Pedal Fade**:
A Setting under which Sustained Keys dim out and go dark a fixed time after release, instead of staying fully lit while the pedal is down.

**Blank On Press**:
A Setting under which pressing a Key whose LED is already lit (it is Sounding, or still Fading) turns that LED off for a brief holdoff before it lights again, so every press shows.

### Control

**Master Brightness**:
The overall brightness of all LEDs, set with the rotary encoder. It can never reach fully off.

**Settings Menu**:
A state, opened with the encoder switch, in which white Keys at the bass end choose Settings instead of lighting up.
_Avoid_: Mode Select

**Option**:
One value of a Setting, assigned to one white Key in the Settings Menu. An on/off Setting has two Options, Off and On.
_Avoid_: Toggle

**Menu Action**:
A Key in the Settings Menu that does something once instead of choosing a value, such as starting Demo Mode.

**Swatch**:
The small animation an Option's LED shows in the Settings Menu, bright when selected and dim when not.

**Preview**:
A brief display on all LEDs that shows how the current Settings look.
_Avoid_: Mode Preview

**Demo Mode**:
A state in which LEDs light at random under the current Settings with no Keys pressed, until someone plays. It is not a Light Mode.
