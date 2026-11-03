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

### Lights

**LED**:
The light assigned to one Key. An LED can be lit without its Key being pressed.

**Fading**:
An LED dimming to off after nothing asks it to stay lit. Applies to LEDs only, never to Keys.

**Light Mode**:
The rule that decides how LEDs respond to Sounding Keys.
_Avoid_: Color mode

### Control

**Master Brightness**:
The overall brightness of all LEDs, set with the rotary encoder. It can never reach fully off.

**Mode Preview**:
A brief display on all LEDs that represents the Light Mode just selected.

**Mode Select**:
The deliberate act of choosing a Light Mode by holding A0 first and then playing a chord.

**Demo Mode**:
A state in which LEDs light at random under the current Light Mode with no Keys pressed, until someone plays. It is not a Light Mode.
